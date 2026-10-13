#pragma once
#include "agent_backend.hpp"
class CodeExecutor {
public:
    int max_execution_timeout = 30;
    int max_install_timeout = 60;
    size_t max_output_bytes = 131072;
    std::string last_compiled_exe;
    std::string run_shell_command(const std::string& command, int timeout = 30) {
        std::string full =
            "timeout " + std::to_string(timeout) + "s " + command +
            " 2>&1; echo RC:$?";
        FILE* pipe = popen(full.c_str(), "r");
        if (!pipe) {
            return "Error: cannot execute shell command";
        }
        std::string output;
        std::string tail_buffer;
        char buffer[512];
        bool truncated = false;
        while (fgets(buffer, sizeof(buffer), pipe)) {
            if (output.size() < max_output_bytes) {
                output += buffer;
            } else {
                if (!truncated) {
                    output +=
                        "\n...[OUTPUT TRUNCATED TO PREVENT MEMORY EXHAUSTION]...\n";
                    truncated = true;
                }
                tail_buffer += buffer;
                if (tail_buffer.size() > 1024) {
                    tail_buffer = tail_buffer.substr(tail_buffer.size() - 512);
                }
            }
        }
        pclose(pipe);
        if (truncated) {
            output += tail_buffer;
        }
        return "Shell output:\n" + output;
    }
    std::string compile_cpp(const std::string& source_file) {
        std::ifstream f(source_file);
        if (!f.is_open()) {
            return "COMPILATION_FAILED: cannot open source file: " + source_file;
        }
        std::string content(
            (std::istreambuf_iterator<char>(f)),
            std::istreambuf_iterator<char>()
        );
        std::string source_key;
        try {
            source_key = fs::absolute(source_file).lexically_normal().string();
        } catch (...) {
            source_key = source_file;
        }
        std::string content_hash = md5_hash(content);
        auto it = cpp_compile_cache_.find(source_key);
        if (it != cpp_compile_cache_.end() &&
            it->second.source_hash == content_hash &&
            fs::exists(it->second.exe_path)) {
            it->second.last_used = get_time();
            last_compiled_exe = it->second.exe_path;
            return last_compiled_exe;
        }
        std::string compile_flags = "-std=c++20 -O2";
        std::string link_flags;
        if (content.find("std::thread") != std::string::npos ||
            content.find("<thread>") != std::string::npos ||
            content.find("<mutex>") != std::string::npos ||
            content.find("<condition_variable>") != std::string::npos) {
            link_flags += " -pthread";
        }
        if (content.find("<curl/curl.h>") != std::string::npos) {
            link_flags += " -lcurl";
        }
        if (content.find("<openssl/") != std::string::npos) {
            link_flags += " -lssl -lcrypto";
        }
        if (content.find("<nlohmann/json.hpp>") != std::string::npos) {
            compile_flags += " -I/usr/include/nlohmann";
        }
        std::string cache_key =
            source_key + "|" + content_hash + "|" + compile_flags + "|" + link_flags;
        std::string out = "/tmp/agent_" + md5_hash(cache_key) + ".out";
        std::string old_exe;
        if (it != cpp_compile_cache_.end()) {
            old_exe = it->second.exe_path;
        }
        std::string cmd = "g++ " + compile_flags + " \"" + source_file +
                          "\" -o \"" + out + "\"" + link_flags;
        std::string output = run_shell_command(cmd, 15);
        if (output.rfind("RC:0") == std::string::npos || !fs::exists(out)) {
            if (fs::exists(out)) {
                fs::remove(out);
            }
            return "COMPILATION_FAILED: " + output;
        }
        last_compiled_exe = out;
        if (it != cpp_compile_cache_.end()) {
            it->second.source_hash = content_hash;
            it->second.exe_path = out;
            it->second.last_used = get_time();
        } else {
            if (cpp_compile_cache_.size() >= max_cpp_compile_cache_) {
                evict_oldest_compile_entry();
            }
            CppCompileCacheEntry entry;
            entry.source_key = source_key;
            entry.source_hash = content_hash;
            entry.exe_path = out;
            entry.last_used = get_time();
            cpp_compile_cache_[source_key] = entry;
        }
        if (!old_exe.empty() && old_exe != out && fs::exists(old_exe)) {
            fs::remove(old_exe);
        }
        return out;
    }
    std::string execute_file(
        const std::string& file_path,
        const std::string& stdin_input = ""
    ) {
        fs::path path(file_path);
        std::string ext = path.extension().string();
        std::string cmd;
        if (
            ext.empty() &&
            !last_compiled_exe.empty() &&
            fs::exists(last_compiled_exe)
        ) {
            cmd = "\"" + last_compiled_exe + "\"";
        } else if (ext == ".py") {
            cmd = "python3 \"" + file_path + "\"";
        } else if (ext == ".cpp" || ext == ".cc" || ext == ".cxx") {
            std::string exe = compile_cpp(file_path);
            if (exe.find("COMPILATION_FAILED:") != std::string::npos) {
                return exe;
            }
            cmd = "\"" + exe + "\"";
        } else if (fs::exists(path) && access(path.c_str(), X_OK) == 0) {
            cmd = "\"" + path.string() + "\"";
        } else {
            return "Unsupported file type: " + ext;
        }
        std::string stdin_file;
        if (!stdin_input.empty()) {
            stdin_file =
                "/tmp/agent_stdin_" + std::to_string(rand()) + ".txt";
            std::ofstream f(stdin_file);
            f << stdin_input;
            f.close();
        }
        std::string full;
        if (!stdin_file.empty()) {
            full =
                "timeout " + std::to_string(max_execution_timeout) + "s " +
                cmd + " < \"" + stdin_file + "\" 2>&1; echo RC:$?";
        } else {
            full =
                "timeout " + std::to_string(max_execution_timeout) + "s " +
                cmd + " 2>&1; echo RC:$?";
        }
        FILE* pipe = popen(full.c_str(), "r");
        if (!pipe) {
            if (!stdin_file.empty()) fs::remove(stdin_file);
            return "Error: cannot execute file";
        }
        std::string output;
        std::string tail_buffer;
        char buffer[512];
        bool truncated = false;
        while (fgets(buffer, sizeof(buffer), pipe)) {
            if (output.size() < max_output_bytes) {
                output += buffer;
            } else {
                if (!truncated) {
                    output +=
                        "\n...[OUTPUT TRUNCATED TO PREVENT MEMORY EXHAUSTION]...\n";
                    truncated = true;
                }
                tail_buffer += buffer;
                if (tail_buffer.size() > 1024) {
                    tail_buffer = tail_buffer.substr(tail_buffer.size() - 512);
                }
            }
        }
        pclose(pipe);
        if (!stdin_file.empty()) {
            fs::remove(stdin_file);
        }
        if (truncated) {
            output += tail_buffer;
        }
        int rc = -1;
        size_t pos = output.rfind("RC:");
        if (pos != std::string::npos) {
            std::string rc_str = output.substr(pos + 3);
            rc = std::atoi(rc_str.c_str());
            output = output.substr(0, pos);
            if (!output.empty() && output.back() == '\n') output.pop_back();
        }
        std::ostringstream oss;
        oss << "Return code: " << rc << "\nSTDOUT:\n" << output;
        return oss.str();
    }
    std::string execute_code(
        const std::string& code,
        const std::string& language = "auto"
    ) {
        std::string lang = language;
        if (lang == "auto") {
            lang =
                (code.find("#include") != std::string::npos ||
                 code.find("int main(") != std::string::npos)
                    ? "cpp"
                    : "python";
        }
        std::string tmp =
            "/tmp/agent_code_" + std::to_string(rand()) +
            ((lang == "cpp") ? ".cpp" : ".py");
        std::ofstream f(tmp);
        f << code;
        f.close();
        std::string result = execute_file(tmp);
        fs::remove(tmp);
        return result;
    }
    std::string validate_syntax(
        const std::string& code,
        const std::string& language = "auto"
    ) {
        std::string lang = language;
        if (lang == "auto") {
            lang =
                (code.find("#include") != std::string::npos ||
                 code.find("int main(") != std::string::npos)
                    ? "cpp"
                    : "python";
        }
        if (lang == "cpp") {
            std::string tmp =
                "/tmp/agent_syntax_" + std::to_string(rand()) + ".cpp";
            std::ofstream f(tmp);
            f << code;
            f.close();
            std::string cmd = "g++ -std=c++20 -fsyntax-only \"" + tmp + "\"";
            std::string out = run_shell_command(cmd, 10);
            fs::remove(tmp);
            if (out.rfind("RC:0") != std::string::npos) {
                return "Syntax validation PASSED.";
            }
            return "Syntax validation FAILED: " + out;
        }
        std::string tmp =
            "/tmp/agent_syntax_" + std::to_string(rand()) + ".py";
        std::ofstream f(tmp);
        f << code;
        f.close();
        std::string cmd = "python3 -m py_compile \"" + tmp + "\"";
        std::string out = run_shell_command(cmd, 10);
        fs::remove(tmp);
        if (out.rfind("RC:0") != std::string::npos) {
            return "Syntax validation PASSED.";
        }
        return "Syntax validation FAILED: " + out;
    }

private:
    struct CppCompileCacheEntry {
        std::string source_key;
        std::string source_hash;
        std::string exe_path;
        double last_used = 0.0;
    };
    static constexpr size_t max_cpp_compile_cache_ = 12;
    std::unordered_map<std::string, CppCompileCacheEntry> cpp_compile_cache_;
    void evict_oldest_compile_entry() {
        if (cpp_compile_cache_.empty()) {
            return;
        }
        auto oldest = cpp_compile_cache_.begin();
        for (auto it = cpp_compile_cache_.begin();
             it != cpp_compile_cache_.end();
             ++it) {
            if (it->second.last_used < oldest->second.last_used) {
                oldest = it;
            }
        }
        if (fs::exists(oldest->second.exe_path)) {
            fs::remove(oldest->second.exe_path);
        }
        cpp_compile_cache_.erase(oldest);
    }
};

struct MemoryEntry {
    std::string task_hash;
    std::string task_text;
    std::string solution_code;
    std::unordered_map<std::string, double> raw_tf;
    double norm = 0.0;
    double success_timestamp = 0.0;
    int api_calls_saved = 0;
    int access_count = 0;
    double last_access_time = 0.0;
    int term_count = 0;
    bool is_plan = false;
    int success_count = 0;
    int failure_count = 0;
    uint32_t doc_id = 0;
};

class SemanticMemoryManager {
public:
    explicit SemanticMemoryManager(const std::string & db_path = "./agent_memory_db")
        : db_path(db_path) {
        fs::create_directories(db_path);
        load_all();
    }
    std::string retrieve_similar(
        const std::string & query,
        double threshold = 0.35
    ) {
        std::string task_hash = md5_hash(query);
        if (auto it = exact.find(task_hash); it != exact.end()) {
            auto entry = it->second;
            double now = get_time();
            entry->access_count++;
            entry->last_access_time = now;
            entry->api_calls_saved++;
            save_all();
            stats.exact_hits++;
            stats.retrieval_count++;
            return entry->solution_code;
        }
        if (memory.empty()) {
            stats.misses++;
            return "";
        }
        auto query_tokens = tokenize(query);
        if (query_tokens.empty()) {
            stats.misses++;
            return "";
        }
        std::unordered_map<std::string, int> query_freq;
        for (const auto & t : query_tokens) query_freq[t]++;
        double now = get_time();
        double eff_threshold = threshold;
        if (auto it = dynamic_thresholds.find(task_hash); it != dynamic_thresholds.end()) {
            eff_threshold = it->second;
        }
        const double k1 = 1.5;
        const double b = 0.75;
        ensure_accumulator_capacity(memory.size());
        current_eval_gen_++;
        active_candidates_.clear();
        for (const auto & [term, q_freq] : query_freq) {
            auto inv_it = inverted_index_.find(term);
            if (inv_it == inverted_index_.end()) continue;
            double idf_val = idf.count(term) ? idf.at(term) : 0.0;
            if (idf_val <= 0.0) continue;
            const auto & postings = inv_it->second.postings;
            for (const auto & posting : postings) {
                uint32_t doc_id = posting.doc_id;
                if (doc_eval_gen_[doc_id] != current_eval_gen_) {
                    doc_eval_gen_[doc_id] = current_eval_gen_;
                    accum_dot_[doc_id] = 0.0;
                    active_candidates_.push_back(doc_id);
                }
                double f = posting.tf;
                double dl = memory[doc_id]->term_count;
                double dl_ratio = (avgdl_ > 0) ? (dl / avgdl_) : 1.0;
                double numerator = f * (k1 + 1.0);
                double denominator = f + k1 * (1.0 - b + b * dl_ratio);
                accum_dot_[doc_id] += idf_val * (numerator / denominator);
            }
        }
        std::shared_ptr<MemoryEntry> best_entry;
        double best_score = 0.0;
        double best_cos = 0.0;
        for (uint32_t doc_id : active_candidates_) {
            if (accum_dot_[doc_id] <= 0.0) continue;
            auto & entry = memory[doc_id];
            double bm25_score = accum_dot_[doc_id];
            double age_days = (now - entry->success_timestamp) / 86400.0;
            double freshness = 1.0 - 0.1 * std::min(1.0, std::max(0.0, (age_days - 1.0) / 6.0));
            double reliability = memory_reliability(entry);
            double normalized_bm25 = bm25_score / (bm25_score + 2.0);
            double score = (0.7 * normalized_bm25 + 0.3 * reliability) * freshness;
            if (score > best_score) {
                best_score = score;
                best_entry = entry;
                best_cos = normalized_bm25;
            }
        }
        if (best_entry && best_score >= eff_threshold) {
            stats.semantic_hits++;
            stats.retrieval_count++;
            best_entry->access_count++;
            best_entry->last_access_time = now;
            best_entry->api_calls_saved++;
            last_retrieval[task_hash] = {best_cos, 0.0, best_entry->task_hash};
            save_all();
            return best_entry->solution_code;
        }
        stats.misses++;
        return "";
    }
    void store(
        const std::string & task,
        const std::string & solution
    ) {
        std::string safe_task = sanitize_utf8(task);
        std::string safe_solution = sanitize_utf8(solution);
        std::string task_hash = md5_hash(safe_task);
        bool is_plan = false;
        try {
            json parsed = json::parse(safe_solution);
            if (parsed.is_array()) {
                is_plan = true;
                for (auto & action : parsed) {
                    if (!action.contains("tool")) {
                        is_plan = false;
                        break;
                    }
                }
            }
        } catch (...) {
        }
        std::shared_ptr<MemoryEntry> entry;
        if (auto it = exact.find(task_hash); it != exact.end()) {
            entry = it->second;
            bool task_text_changed = (entry->task_text != safe_task);
            entry->solution_code = safe_solution;
            if (task_text_changed) {
                entry->task_text = safe_task;
                compute_raw_tf(entry);
                rebuild_index();
            }
        } else {
            entry = std::make_shared<MemoryEntry>();
            entry->task_hash = task_hash;
            entry->task_text = safe_task;
            entry->solution_code = safe_solution;
            compute_raw_tf(entry);
            if (static_cast<int>(memory.size()) >= max_entries) {
                evict_lowest_scoring_entry();
            }
            entry->doc_id = static_cast<uint32_t>(memory.size());
            memory.push_back(entry);
            exact[task_hash] = entry;
            rebuild_index();
        }
        entry->success_timestamp = get_time();
        entry->is_plan = is_plan;
        entry->access_count++;
        save_all();
    }
    void store_failure(
        const std::string & task_hash,
        const std::string & error_type,
        const std::string & error_output,
        const std::vector<std::string>& fix_actions,
        const std::string & hint
    ) {
        std::string sig = error_output.substr(0, 200);
        std::replace(sig.begin(), sig.end(), '\n', ' ');
        failure_patterns.push_back({
            task_hash,
            error_type,
            sig,
            fix_actions,
            hint,
            1,
            get_time()
        });
        save_failures();
    }
    std::string get_hint_for_error(const std::string & error_output) {
        std::string etype = error_type_from_output(error_output);
        std::string sig = error_output.substr(0, 200);
        std::replace(sig.begin(), sig.end(), '\n', ' ');
        for (auto & pattern : failure_patterns) {
            bool same_type = pattern.error_type == etype;
            bool signature_matches =
                pattern.error_signature.find(sig) != std::string::npos ||
                sig.find(pattern.error_signature) != std::string::npos;
            if (same_type && signature_matches) {
                pattern.occurrence_count++;
                pattern.last_seen = get_time();
                save_failures();
                return pattern.hint;
            }
        }
        return "";
    }
    void report_outcome(
        const std::string & task_hash,
        bool success
    ) {
        auto it = last_retrieval.find(task_hash);
        if (it != last_retrieval.end()) {
            auto [cos, jac, entry_hash] = it->second;
            if (auto entry_it = exact.find(entry_hash);
                entry_it != exact.end()) {
                auto entry = entry_it->second;
                if (success) {
                    entry->success_count++;
                } else {
                    entry->failure_count++;
                }
                save_all();
            }
            last_retrieval.erase(it);
        }
        double thresh = 0.35;
        if (success) {
            thresh = std::max(0.20, thresh * 0.95);
        } else {
            consec_fail[task_hash]++;
            if (consec_fail[task_hash] >= 2) {
                thresh = std::min(0.50, thresh * 1.05);
                consec_fail[task_hash] = 0;
            }
        }
        dynamic_thresholds[task_hash] = thresh;
    }
    void print_stats() const {
        int saved = 0;
        for (const auto & entry : memory) {
            saved += entry->api_calls_saved;
        }
        std::cout << "Memory entries: " << memory.size() << "/" << max_entries << "\n";
        std::cout << "Hits exact=" << stats.exact_hits
                  << " semantic=" << stats.semantic_hits
                  << " misses=" << stats.misses << "\n";
        std::cout << "API calls saved: " << saved << "\n";
    }

private:
    struct CompactPosting {
        uint32_t doc_id;
        double tf;
    };
    struct InvertedList {
        std::vector<CompactPosting> postings;
        double max_tf_idf = 0.0;
    };
    struct FailurePattern {
        std::string task_hash;
        std::string error_type;
        std::string error_signature;
        std::vector<std::string> fix_actions;
        std::string hint;
        int occurrence_count = 1;
        double last_seen = 0.0;
    };
    struct Stats {
        int exact_hits = 0;
        int semantic_hits = 0;
        int misses = 0;
        int retrieval_count = 0;
    };
    std::vector<std::shared_ptr<MemoryEntry>> memory;
    std::unordered_map<std::string, std::shared_ptr<MemoryEntry>> exact;
    std::unordered_map<std::string, InvertedList> inverted_index_;
    std::unordered_map<std::string, double> idf;
    std::unordered_map<std::string, int> doc_freq_;
    int total_docs = 0;
    double avgdl_ = 0.0;
    Stats stats;
    std::unordered_map<std::string, double> dynamic_thresholds;
    int max_entries = 50;
    std::string db_path;
    std::vector<FailurePattern> failure_patterns;
    std::unordered_map<
        std::string,
        std::tuple<double, double, std::string>
    > last_retrieval;
    double weights[3] = {3.0, 2.0, -1.5};
    double learning_rate = 0.02;
    std::unordered_map<std::string, int> consec_fail;
    std::vector<double> accum_dot_;
    std::vector<int> accum_overlap_;
    std::vector<uint32_t> doc_eval_gen_;
    std::vector<double> min_dot_req_;
    std::vector<uint32_t> active_candidates_;
    uint32_t current_eval_gen_ = 0;
    static std::string sanitize_utf8(const std::string & input) {
        std::string result;
        result.reserve(input.size());
        size_t i = 0;
        while (i < input.size()) {
            unsigned char c = input[i];
            size_t len = 1;
            if (c < 0x80) len = 1;
            else if ((c >> 5) == 0x06) len = 2;
            else if ((c >> 4) == 0x0E) len = 3;
            else if ((c >> 3) == 0x1E) len = 4;
            else {
                result += '?';
                i++;
                continue;
            }
            if (i + len > input.size()) {
                break;
            }
            bool valid = true;
            for (size_t j = 1; j < len; ++j) {
                if ((input[i + j] & 0xC0) != 0x80) {
                    valid = false;
                    break;
                }
            }
            if (valid) {
                result.append(input, i, len);
            } else {
                result += '?';
            }
            i += len;
        }
        return result;
    }
    void ensure_accumulator_capacity(size_t n) {
        if (accum_dot_.size() < n) {
            accum_dot_.resize(n, 0.0);
            accum_overlap_.resize(n, 0);
            doc_eval_gen_.resize(n, 0);
            min_dot_req_.resize(n, 0.0);
        }
    }
    void load_all() {
        load_memory();
        load_failures();
    }
    void load_memory() {
        fs::path mem_file = fs::path(db_path) / "semantic_memory.json";
        if (!fs::exists(mem_file)) {
            return;
        }
        try {
            std::ifstream f(mem_file);
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            content = sanitize_utf8(content);
            json data = json::parse(content, nullptr, false);
            if (data.is_discarded()) {
                return;
            }
            memory.clear();
            exact.clear();
            for (auto & item : data) {
                auto entry = std::make_shared<MemoryEntry>();
                entry->task_hash = item.value("task_hash", "");
                entry->task_text = item.value("task_text", "");
                entry->solution_code = item.value("solution_code", "");
                entry->success_timestamp = item.value("timestamp", 0.0);
                entry->api_calls_saved = item.value("api_calls_saved", 0);
                entry->access_count = item.value("access_count", 0);
                entry->last_access_time = item.value("last_access_time", 0.0);
                entry->is_plan = item.value("is_plan", false);
                entry->success_count = item.value("success_count", 0);
                entry->failure_count = item.value("failure_count", 0);
                entry->doc_id = static_cast<uint32_t>(memory.size());
                compute_raw_tf(entry);
                memory.push_back(entry);
                exact[entry->task_hash] = entry;
            }
            rebuild_index();
        } catch (const std::exception & e) {
            std::cerr << "[Memory] Load error: " << e.what() << "\n";
        }
    }
    void load_failures() {
        fs::path fail_file = fs::path(db_path) / "failure_patterns.json";
        if (!fs::exists(fail_file)) {
            return;
        }
        try {
            std::ifstream f(fail_file);
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            content = sanitize_utf8(content);
            json data = json::parse(content, nullptr, false);
            if (data.is_discarded()) {
                return;
            }
            for (auto & item : data) {
                FailurePattern pattern;
                pattern.task_hash = item.value("task_hash", "");
                pattern.error_type = item.value("error_type", "");
                pattern.error_signature = item.value("error_signature", "");
                if (item.contains("fix_actions")) {
                    for (auto & action : item["fix_actions"]) {
                        pattern.fix_actions.push_back(action);
                    }
                }
                pattern.hint = item.value("hint", "");
                pattern.occurrence_count = item.value("occurrence_count", 1);
                pattern.last_seen = item.value("last_seen", 0.0);
                failure_patterns.push_back(pattern);
            }
        } catch (...) {
        }
    }
    void save_all() {
        save_memory();
        save_failures();
    }
    void save_memory() {
        json data = json::array();
        for (const auto & entry : memory) {
            data.push_back({
                {"task_hash", entry->task_hash},
                {"task_text", entry->task_text},
                {"solution_code", entry->solution_code},
                {"timestamp", entry->success_timestamp},
                {"api_calls_saved", entry->api_calls_saved},
                {"access_count", entry->access_count},
                {"last_access_time", entry->last_access_time},
                {"is_plan", entry->is_plan},
                {"success_count", entry->success_count},
                {"failure_count", entry->failure_count}
            });
        }
        std::ofstream(fs::path(db_path) / "semantic_memory.json") << data.dump(2);
    }
    void save_failures() {
        json data = json::array();
        for (const auto & pattern : failure_patterns) {
            json fixes = json::array();
            for (const auto & fix : pattern.fix_actions) {
                fixes.push_back(fix);
            }
            data.push_back({
                {"task_hash", pattern.task_hash},
                {"error_type", pattern.error_type},
                {"error_signature", pattern.error_signature},
                {"fix_actions", fixes},
                {"hint", pattern.hint},
                {"occurrence_count", pattern.occurrence_count},
                {"last_seen", pattern.last_seen}
            });
        }
        std::ofstream(fs::path(db_path) / "failure_patterns.json") << data.dump(2);
    }
    void compute_raw_tf(std::shared_ptr<MemoryEntry> entry) {
        entry->raw_tf.clear();
        auto tokens = tokenize(entry->task_text);
        if (tokens.empty()) {
            entry->term_count = 0;
            entry->norm = 0.0;
            return;
        }
        std::unordered_map<std::string, int> freq;
        for (const auto & token : tokens) {
            freq[token]++;
        }
        int max_freq = 0;
        for (const auto & [_, count] : freq) {
            max_freq = std::max(max_freq, count);
        }
        for (const auto & [term, count] : freq) {
            entry->raw_tf[term] = static_cast<double>(count) / max_freq;
        }
        entry->term_count = static_cast<int>(entry->raw_tf.size());
    }
    void rebuild_index() {
        total_docs = static_cast<int>(memory.size());
        doc_freq_.clear();
        idf.clear();
        inverted_index_.clear();
        double total_terms = 0.0;
        for (uint32_t i = 0; i < memory.size(); ++i) {
            memory[i]->doc_id = i;
            total_terms += memory[i]->term_count;
            for (const auto & [term, _] : memory[i]->raw_tf) {
                doc_freq_[term]++;
            }
        }
        avgdl_ = total_docs > 0 ? total_terms / total_docs : 1.0;
        for (const auto & [term, df] : doc_freq_) {
            idf[term] = std::log(1.0 + (total_docs - df + 0.5) / (df + 0.5));
        }
        for (uint32_t i = 0; i < memory.size(); ++i) {
            auto & entry = memory[i];
            double nsq = 0.0;
            for (const auto & [term, raw_tf] : entry->raw_tf) {
                double idf_val = idf[term];
                double w = raw_tf * idf_val;
                nsq += w * w;
                auto & inv_list = inverted_index_[term];
                inv_list.postings.push_back({i, raw_tf});
                inv_list.max_tf_idf = std::max(inv_list.max_tf_idf, w);
            }
            entry->norm = std::sqrt(nsq);
        }
    }
    void evict_lowest_scoring_entry() {
        if (memory.empty()) return;
        auto to_evict = std::min_element(
            memory.begin(),
            memory.end(),
            [&](const auto & a, const auto & b) {
                double score_a = 0.65 * std::min(a->access_count, 10) / 10.0 + 0.35 * memory_reliability(a);
                double score_b = 0.65 * std::min(b->access_count, 10) / 10.0 + 0.35 * memory_reliability(b);
                return score_a < score_b;
            }
        );
        if (to_evict != memory.end()) {
            exact.erase((*to_evict)->task_hash);
            memory.erase(to_evict);
        }
    }
    double memory_reliability(const std::shared_ptr<MemoryEntry> & entry) const {
        int total = entry->success_count + entry->failure_count;
        if (total <= 0) {
            return 0.5;
        }
        return (entry->success_count + 1.0) / (total + 2.0);
    }
};

class FileManager {
public:
    explicit FileManager(const std::string &backup_dir) : backup_dir(backup_dir) {
        fs::create_directories(backup_dir);
    }
    std::string write_file(const std::string &file_path, const std::string &content) {
        fs::path path(file_path);
        if (path.has_parent_path()) {
            fs::create_directories(path.parent_path());
        }
        std::string fixed = content;
        fixed = std::regex_replace(
            fixed,
            std::regex(R"(#include\s*<json/json\.h>)"),
            "#include <nlohmann/json.hpp>"
        );
        fs::path tmp = path.string() + ".tmp";
        {
            std::ofstream f(tmp);
            if (!f.is_open()) {
                return "Error: Cannot create temporary file for writing: " + tmp.string();
            }
            f << fixed;
        }
        try {
            fs::rename(tmp, path);
        } catch (const std::exception &e) {
            return "Error: Failed to rename temporary file for " + file_path +
                   ": " + std::string(e.what());
        }
        return "File written successfully: " + file_path;
    }
    std::string read_file(const std::string &file_path) const {
        fs::path path(file_path);
        if (!fs::exists(path)) {
            return "Error: File not found: " + file_path;
        }
        if (!fs::is_regular_file(path)) {
            return "Error: Not a regular file: " + file_path;
        }
        try {
            std::ifstream f(file_path, std::ios::binary);
            if (!f.is_open()) {
                return "Error: Cannot open file: " + file_path;
            }
            std::ostringstream ss;
            ss << f.rdbuf();
            if (f.bad()) {
                return "Error reading file: " + file_path;
            }
            std::string content = ss.str();
            if (content.empty()) {
                return "Error: File is empty: " + file_path;
            }
            return content;
        } catch (const std::exception &e) {
            return "Error reading file: " + std::string(e.what());
        }
    }
    std::string backup_file(const std::string &file_path) const {
        fs::path path(file_path);
        if (!fs::exists(path)) {
            return "";
        }
        const std::string base_name = path.filename().string();
        std::lock_guard<std::mutex> lock(version_mutex_);
        int next_ver = next_version_locked(base_name);
        fs::path backup = fs::path(backup_dir) /
                          (base_name + "_v" + std::to_string(next_ver) + ".bak");
        fs::copy_file(path, backup, fs::copy_options::overwrite_existing);
        return backup.string();
    }

private:
    fs::path backup_dir;
    mutable std::mutex version_mutex_;
    mutable std::unordered_map<std::string, int> next_version_;
    mutable bool version_index_initialized_ = false;
    static bool extract_backup_base_version(const std::string &file_name,
                                            std::string &base,
                                            int &version) {
        const std::string suffix = ".bak";
        if (file_name.size() <= suffix.size() ||
            file_name.compare(file_name.size() - suffix.size(), suffix.size(), suffix) != 0) {
            return false;
        }
        const std::string core = file_name.substr(0, file_name.size() - suffix.size());
        const size_t sep = core.rfind("_v");
        if (sep == std::string::npos || sep + 2 >= core.size()) {
            return false;
        }
        const std::string version_text = core.substr(sep + 2);
        for (char c : version_text) {
            if (c < '0' || c > '9') {
                return false;
            }
        }
        base = core.substr(0, sep);
        version = std::atoi(version_text.c_str());
        return true;
    }
    void build_version_index_locked() const {
        if (version_index_initialized_) {
            return;
        }
        next_version_.clear();
        for (const auto &entry : fs::directory_iterator(backup_dir)) {
            const std::string name = entry.path().filename().string();
            std::string base;
            int version = 0;
            if (extract_backup_base_version(name, base, version)) {
                int &next = next_version_[base];
                next = std::max(next, version + 1);
            }
        }
        version_index_initialized_ = true;
    }
    int next_version_locked(const std::string &base_name) const {
        build_version_index_locked();
        auto it = next_version_.find(base_name);
        if (it == next_version_.end()) {
            next_version_[base_name] = 1;
            return 1;
        }
        return it->second++;
    }
};

class ToolExecutor {
public:
    ToolExecutor() {}
    CodeExecutor executor;
    FileManager file_manager{".agent_backups"};
    std::string execute_action_dict(json action) {
        try {
            if (!action.is_object()) {
                return "Error: Tool action is not a JSON object.";
            }
            action = apply_aliases(action);
            if (!action.contains("tool") || !action["tool"].is_string()) {
                return "Error: Tool action is missing a string 'tool' field.";
            }
            std::string tool = action["tool"].get<std::string>();
            json args = (action.contains("args") && action["args"].is_object())
                            ? action["args"]
                            : json::object();
            static const std::map<std::string, std::set<std::string>> required = {
                {"write_file", {"file_path", "content"}},
                {"read_file", {"file_path"}},
                {"execute_file", {"file_path"}},
                {"compile_cpp", {"file_path"}},
                {"run_shell_command", {"command"}},
                {"make_directory", {"dir_path"}},
                {"install_package", {"package"}},
                {"finish", {}}
            };
            if (!required.count(tool)) {
                return "Error: Tool '" + tool + "' is not recognized.";
            }
            std::set<std::string> missing;
            for (const auto& arg : required.at(tool)) {
                if (!args.contains(arg)) {
                    missing.insert(arg);
                }
            }
            if (!missing.empty()) {
                std::string msg;
                for (const auto& m : missing) {
                    msg += m + " ";
                }
                return "Error: Missing required arguments for '" + tool +
                       "': " + msg;
            }
            if (tool == "write_file") {
                std::string fp = get_string_arg(args, "file_path");
                std::string content = get_string_arg(args, "content");
                if (fp.empty()) {
                    return "Error: write_file 'file_path' must be a non-empty string.";
                }
                if (content.empty()) {
                    return "Error: write_file 'content' must be a non-empty string.";
                }
                std::string syntax = validate_content_syntax(fp, content);
                if (!syntax.empty() &&
                    syntax.find("FAILED") != std::string::npos) {
                    return "Error: " + syntax +
                           "\nFile was NOT written. Fix the syntax error and try again.";
                }
                return file_manager.write_file(fp, content);
            }
            if (tool == "read_file") {
                std::string fp = get_string_arg(args, "file_path");
                if (fp.empty()) {
                    return "Error: read_file 'file_path' must be a non-empty string.";
                }
                return file_manager.read_file(fp);
            }
            if (tool == "execute_file") {
                std::string fp = get_string_arg(args, "file_path");
                if (fp.empty()) {
                    return "Error: execute_file 'file_path' must be a non-empty string.";
                }
                std::string stdin_input = get_string_arg(args, "stdin");
                return executor.execute_file(fp, stdin_input);
            }
            if (tool == "make_directory") {
                std::string dir = get_string_arg(args, "dir_path");
                if (dir.empty()) {
                    return "Error: make_directory 'dir_path' must be a non-empty string.";
                }
                fs::create_directories(dir);
                return "Directory created: " + dir;
            }
            if (tool == "install_package") {
                std::string pkg = get_string_arg(args, "package");
                if (pkg.empty()) {
                    return "Error: install_package 'package' must be a non-empty string.";
                }
                return executor.run_shell_command(
                    "pip install " + pkg,
                    executor.max_install_timeout
                );
            }
            if (tool == "compile_cpp") {
                std::string fp = get_string_arg(args, "file_path");
                if (fp.empty()) {
                    return "Error: compile_cpp 'file_path' must be a non-empty string.";
                }
                return executor.compile_cpp(fp);
            }
            if (tool == "run_shell_command") {
                std::string cmd = get_string_arg(args, "command");
                if (cmd.empty()) {
                    return "Error: run_shell_command 'command' must be a non-empty string.";
                }
                return executor.run_shell_command(cmd);
            }
            if (tool == "finish") {
                return "Task marked as finished. Verifying results...";
            }
            return "Unknown tool";
        } catch (const std::exception& e) {
            return "Exception executing tool: " + std::string(e.what());
        }
    }
    std::vector<json> extract_actions(const std::string& text) {
        std::vector<json> objects;
        std::set<std::string> seen;
        auto add_object = [&](const json& obj) {
            if (obj.is_object() && obj.contains("tool")) {
                json normalized = apply_aliases(obj);
                std::string key = normalized.dump();
                if (!seen.count(key)) {
                    seen.insert(key);
                    objects.push_back(normalized);
                }
            }
        };
        size_t pos = 0;
        while ((pos = text.find("```", pos)) != std::string::npos) {
            size_t end_pos = text.find("```", pos + 3);
            if (end_pos == std::string::npos) {
                break;
            }
            std::string block = text.substr(pos + 3, end_pos - pos - 3);
            size_t start_idx = 0;
            while (start_idx < block.size() &&
                   std::isspace(static_cast<unsigned char>(block[start_idx]))) {
                start_idx++;
            }
            if (block.compare(start_idx, 4, "json") == 0) {
                start_idx += 4;
            }
            parse_and_collect_json(block.substr(start_idx), add_object);
            pos = end_pos + 3;
        }
        if (objects.empty()) {
            parse_and_collect_json(text, add_object);
        }
        if (objects.empty()) {
            json direct = extract_write_file_direct(text);
            if (!direct.is_null() && !direct.empty()) {
                add_object(direct);
            }
        }
        if (objects.empty()) {
            auto xml_actions = extract_xml_tool_calls(text);
            for (const auto& act : xml_actions) {
                add_object(act);
            }
        }
        return objects;
    }

private:
    mutable std::mutex syntax_validation_mutex_;
    std::unordered_map<std::string, std::string> syntax_validation_cache_;
    static std::string get_string_arg(
        const json& args,
        const std::string& key
    ) {
        if (!args.is_object() ||
            !args.contains(key) ||
            !args[key].is_string()) {
            return "";
        }
        return args[key].get<std::string>();
    }
    static json apply_aliases(json action) {
        if (!action.is_object()) {
            return action;
        }
        if ((!action.contains("tool") || !action["tool"].is_string()) &&
            action.contains("name") && action["name"].is_string()) {
            action["tool"] = action["name"];
            action.erase("name");
        }
        if (!action.contains("args") || !action["args"].is_object()) {
            action["args"] = json::object();
        }
        std::vector<std::string> to_move;
        for (auto it = action.begin(); it != action.end(); ++it) {
            if (it.key() != "tool" && it.key() != "args") {
                to_move.push_back(it.key());
            }
        }
        for (const auto& k : to_move) {
            action["args"][k] = action[k];
            action.erase(k);
        }
        auto& args = action["args"];
        if (args.contains("path") && !args.contains("file_path")) {
            args["file_path"] = args["path"];
            args.erase("path");
        }
        if (args.contains("filename") && !args.contains("file_path")) {
            args["file_path"] = args["filename"];
            args.erase("filename");
        }
        if (args.contains("dir") && !args.contains("dir_path")) {
            args["dir_path"] = args["dir"];
            args.erase("dir");
        }
        if (args.contains("directory") && !args.contains("dir_path")) {
            args["dir_path"] = args["directory"];
            args.erase("directory");
        }
        if (args.contains("cmd") && !args.contains("command")) {
            args["command"] = args["cmd"];
            args.erase("cmd");
        }
        if (args.contains("pkg") && !args.contains("package")) {
            args["package"] = args["pkg"];
            args.erase("pkg");
        }
        return action;
    }
    std::string validate_content_syntax(const std::string& file_path,
                                        const std::string& content) {
        fs::path p(file_path);
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".cpp" && ext != ".cc" && ext != ".cxx" &&
            ext != ".hpp" && ext != ".h" && ext != ".py") {
            return "";
        }
        std::string key = file_path + "|" + md5_hash(content);
        {
            std::lock_guard<std::mutex> lock(syntax_validation_mutex_);
            auto it = syntax_validation_cache_.find(key);
            if (it != syntax_validation_cache_.end()) {
                return it->second;
            }
        }
        std::string result;
        if (ext == ".py") {
            std::string tmp = "/tmp/agent_syntax_check_" + std::to_string(rand()) + ".py";
            {
                std::ofstream f(tmp);
                f << content;
            }
            std::string out = executor.run_shell_command(
                "python3 -m py_compile \"" + tmp + "\"", 10);
            fs::remove(tmp);
            if (out.rfind("RC:0") != std::string::npos) {
                result = "Syntax validation PASSED.";
            } else {
                result = "Syntax validation FAILED: " + out;
            }
        } else {
            std::string tmp = "/tmp/agent_syntax_check_" + std::to_string(rand()) + ".cpp";
            {
                std::ofstream f(tmp);
                f << content;
            }
            std::string out = executor.run_shell_command(
                "g++ -std=c++20 -fsyntax-only \"" + tmp + "\"", 10);
            fs::remove(tmp);
            if (out.rfind("RC:0") != std::string::npos) {
                result = "Syntax validation PASSED.";
            } else {
                result = "Syntax validation FAILED: " + out;
            }
        }
        {
            std::lock_guard<std::mutex> lock(syntax_validation_mutex_);
            if (syntax_validation_cache_.size() > 64) {
                syntax_validation_cache_.clear();
            }
            syntax_validation_cache_[key] = result;
        }
        return result;
    }
    void parse_and_collect_json(
        const std::string& input,
        std::function<void(const json&)> callback
    ) {
        std::vector<size_t> open_braces;
        std::vector<std::pair<size_t, size_t>> blocks;
        bool in_str = false;
        bool escape = false;
        for (size_t i = 0; i < input.size(); ++i) {
            char ch = input[i];
            if (escape) {
                escape = false;
                continue;
            }
            if (ch == '\\') {
                escape = true;
                continue;
            }
            if (ch == '"') {
                in_str = !in_str;
                continue;
            }
            if (in_str) {
                continue;
            }
            if (ch == '{') {
                open_braces.push_back(i);
            } else if (ch == '}') {
                if (!open_braces.empty()) {
                    blocks.push_back({open_braces.back(), i});
                    open_braces.pop_back();
                }
            }
        }
        std::sort(
            blocks.begin(),
            blocks.end(),
            [](const auto& a, const auto& b) {
                return (a.second - a.first) > (b.second - b.first);
            }
        );
        std::vector<std::pair<size_t, size_t>> consumed;
        std::function<void(const json&)> extract_all =
            [&](const json& j) {
                if (j.is_object()) {
                    callback(j);
                    for (auto it = j.begin(); it != j.end(); ++it) {
                        extract_all(it.value());
                    }
                } else if (j.is_array()) {
                    for (const auto& val : j) {
                        extract_all(val);
                    }
                }
            };
        for (const auto& block_span : blocks) {
            size_t start = block_span.first;
            size_t end = block_span.second;
            bool is_consumed = false;
            for (const auto& c : consumed) {
                if (start >= c.first && end <= c.second) {
                    is_consumed = true;
                    break;
                }
            }
            if (is_consumed) {
                continue;
            }
            std::string text_block =
                input.substr(start, end - start + 1);

            if (text_block.find("tool") == std::string::npos &&
                text_block.find("name") == std::string::npos) {
                continue;
            }
            bool success = false;
            json parsed = json::parse(text_block, nullptr, false);
            if (!parsed.is_discarded()) {
                extract_all(parsed);
                success = true;
            } else {
                std::string repaired = repair_json_fragment(text_block);
                json parsed2 = json::parse(repaired, nullptr, false);
                if (!parsed2.is_discarded()) {
                    extract_all(parsed2);
                    success = true;
                }
            }
            if (success) {
                consumed.push_back(block_span);
            }
        }
    }
    std::string repair_json_fragment(const std::string& fragment) {
        std::string result;
        result.reserve(fragment.size() + fragment.size() / 10);
        bool in_string = false;
        bool escape = false;
        for (size_t i = 0; i < fragment.size(); ++i) {
            char ch = fragment[i];
            if (escape) {
                result += ch;
                escape = false;
                continue;
            }
            if (ch == '\\') {
                result += ch;
                escape = true;
                continue;
            }
            if (ch == '"') {
                in_string = !in_string;
                result += ch;
                continue;
            }
            if (in_string && (ch == '\n' || ch == '\r')) {
                result += (ch == '\n') ? "\\n" : "\\r";
                continue;
            }
            result += ch;
        }
        std::string no_trailing;
        no_trailing.reserve(result.size());
        for (size_t i = 0; i < result.size(); ++i) {
            if (result[i] == ',') {
                bool trailing = false;
                for (size_t j = i + 1; j < result.size(); ++j) {
                    if (std::isspace(
                            static_cast<unsigned char>(result[j]))) {
                        continue;
                    }
                    if (result[j] == '}' || result[j] == ']') {
                        trailing = true;
                    }
                    break;
                }
                if (trailing) {
                    continue;
                }
            }
            no_trailing += result[i];
        }
        std::string fixed;
        fixed.reserve(no_trailing.size());
        for (size_t i = 0; i < no_trailing.size(); ++i) {
            fixed += no_trailing[i];
            if (no_trailing[i] == '{' || no_trailing[i] == ',') {
                size_t j = i + 1;
                while (j < no_trailing.size() &&
                       std::isspace(
                           static_cast<unsigned char>(no_trailing[j]))) {
                    j++;
                }
                if (j < no_trailing.size() &&
                    (std::isalpha(
                         static_cast<unsigned char>(no_trailing[j])) ||
                     no_trailing[j] == '_')) {
                    size_t k = j;
                    while (k < no_trailing.size() &&
                           (std::isalnum(
                                static_cast<unsigned char>(no_trailing[k])) ||
                            no_trailing[k] == '_')) {
                        k++;
                    }
                    size_t l = k;
                    while (l < no_trailing.size() &&
                           std::isspace(
                               static_cast<unsigned char>(no_trailing[l]))) {
                        l++;
                    }
                    if (l < no_trailing.size() &&
                        no_trailing[l] == ':') {
                        fixed += no_trailing.substr(i + 1, j - i - 1);
                        fixed += '"';
                        fixed += no_trailing.substr(j, k - j);
                        fixed += '"';
                        i = k - 1;
                    }
                }
            }
        }
        return fixed;
    }
    json extract_write_file_direct(const std::string& text) {
        size_t fp_pos = text.find("\"file_path\"");
        if (fp_pos == std::string::npos) {
            return json::object();
        }
        size_t colon = text.find(':', fp_pos + 11);
        if (colon == std::string::npos) {
            return json::object();
        }
        size_t quote = text.find('"', colon + 1);
        if (quote == std::string::npos) {
            return json::object();
        }
        std::string file_path;
        bool escape = false;
        size_t end_quote = quote + 1;
        while (end_quote < text.size()) {
            if (escape) {
                file_path += text[end_quote];
                escape = false;
            } else if (text[end_quote] == '\\') {
                escape = true;
            } else if (text[end_quote] == '"') {
                break;
            } else {
                file_path += text[end_quote];
            }
            end_quote++;
        }
        if (end_quote >= text.size()) {
            return json::object();
        }
        size_t content_pos = text.find("\"content\"", end_quote);
        if (content_pos == std::string::npos) {
            return json::object();
        }
        size_t c_colon = text.find(':', content_pos + 9);
        if (c_colon == std::string::npos) {
            return json::object();
        }
        size_t c_quote = text.find('"', c_colon + 1);
        if (c_quote == std::string::npos) {
            return json::object();
        }
        size_t c_end = text.rfind('"');
        if (c_end <= c_quote) {
            return json::object();
        }
        std::string content =
            text.substr(c_quote + 1, c_end - c_quote - 1);
        return {
            {"tool", "write_file"},
            {
                "args",
                {
                    {"file_path", file_path},
                    {"content", content}
                }
            }
        };
    }
    std::vector<json> extract_xml_tool_calls(const std::string& text) {
        std::vector<json> actions;
        size_t pos = 0;
        while ((pos = text.find("<tool_call", pos)) != std::string::npos) {
            size_t tag_end = text.find(">", pos);
            if (tag_end == std::string::npos) {
                break;
            }
            size_t end_tag = text.find("</tool_call>", tag_end);
            if (end_tag == std::string::npos) {
                break;
            }
            std::string full_tag =
                text.substr(pos, tag_end - pos + 1);
            std::string inner =
                text.substr(tag_end + 1, end_tag - tag_end - 1);
            std::string tool;
            size_t name_pos = inner.find("<tool_name>");
            if (name_pos != std::string::npos) {
                size_t name_end =
                    inner.find("</tool_name>", name_pos);
                if (name_end != std::string::npos) {
                    tool = inner.substr(
                        name_pos + 11,
                        name_end - name_pos - 11
                    );
                }
            }
            if (tool.empty()) {
                size_t n_pos = full_tag.find("name=\"");
                if (n_pos != std::string::npos) {
                    size_t n_end =
                        full_tag.find("\"", n_pos + 6);
                    if (n_end != std::string::npos) {
                        tool = full_tag.substr(
                            n_pos + 6,
                            n_end - n_pos - 6
                        );
                    }
                }
            }
            if (tool.empty()) {
                size_t t_pos = full_tag.find("tool=\"");
                if (t_pos != std::string::npos) {
                    size_t t_end =
                        full_tag.find("\"", t_pos + 6);
                    if (t_end != std::string::npos) {
                        tool = full_tag.substr(
                            t_pos + 6,
                            t_end - t_pos - 6
                        );
                    }
                }
            }
            if (!tool.empty()) {
                json args = json::parse(inner, nullptr, false);
                if (args.is_discarded()) {
                    args = json::parse(repair_json_fragment(inner), nullptr, false);
                    if (args.is_discarded()) {
                        args = json::object();
                        size_t path_pos = inner.find("<file_path>");
                        if (path_pos != std::string::npos) {
                            size_t path_end =
                                inner.find("</file_path>", path_pos);
                            if (path_end != std::string::npos) {
                                args["file_path"] = inner.substr(
                                    path_pos + 11,
                                    path_end - path_pos - 11
                                );
                            }
                        }
                        size_t cont_pos = inner.find("<content>");
                        if (cont_pos != std::string::npos) {
                            size_t cont_end =
                                inner.find("</content>", cont_pos);
                            if (cont_end != std::string::npos) {
                                args["content"] = inner.substr(
                                    cont_pos + 9,
                                    cont_end - cont_pos - 9
                                );
                            }
                        }
                        size_t cmd_pos = inner.find("<command>");
                        if (cmd_pos != std::string::npos) {
                            size_t cmd_end =
                                inner.find("</command>", cmd_pos);
                            if (cmd_end != std::string::npos) {
                                args["command"] = inner.substr(
                                    cmd_pos + 9,
                                    cmd_end - cmd_pos - 9
                                );
                            }
                        }
                    }
                }
                json action;
                action["tool"] = tool;
                if (!args.empty()) {
                    action["args"] = args;
                }
                actions.push_back(action);
            }
            pos = end_tag + 12;
        }
        return actions;
    }
};

class TaskDecomposer {
public:
    explicit TaskDecomposer(LLMBackendManager &llm) : llm(llm) {}
    std::vector<std::string> decompose(const std::string &task) {
        json schema = {{"checklist", json::array({"sub-goal 1"})}};
        std::string prompt =
            "Break this task into 3-5 sequential sub-goals. Output ONLY JSON.\nTask: " +
            task;
        json res = llm.structured_chat({{"user", prompt}}, schema, 0.0, 256);
        if (res.contains("checklist") && res["checklist"].is_array()) {
            std::vector<std::string> goals;
            for (auto &g : res["checklist"]) {
                if (g.is_string()) {
                    goals.push_back(g.get<std::string>());
                }
            }
            return goals;
        }
        return {task};
    }
    std::vector<std::string> decompose_plan(const std::vector<json>& plan) {
        if (plan.empty()) {
            return {};
        }
        std::vector<std::string> goals;
        std::set<std::string> seen_files;
        std::set<std::string> seen_dirs;
        bool finish_seen = false;
        auto arg_str = [](const json& action, const char* key) -> std::string {
            if (!action.contains("args") || !action["args"].is_object()) {
                return "";
            }
            const json& args = action["args"];
            if (!args.contains(key) || !args[key].is_string()) {
                return "";
            }
            return args[key].get<std::string>();
        };
        for (const auto& action : plan) {
            if (!action.is_object()) {
                continue;
            }
            std::string tool = action.value("tool", "");
            if (tool == "make_directory") {
                std::string dir = arg_str(action, "dir_path");
                if (!dir.empty() && seen_dirs.insert(dir).second) {
                    goals.push_back("Create directory structure: " + dir);
                }
            } else if (tool == "write_file") {
                std::string fp = arg_str(action, "file_path");
                if (!fp.empty() && seen_files.insert(fp).second) {
                    goals.push_back("Implement " +
                                    fs::path(fp).filename().string());
                }
            } else if (tool == "compile_cpp") {
                std::string fp = arg_str(action, "file_path");
                if (!fp.empty()) {
                    goals.push_back("Compile " +
                                    fs::path(fp).filename().string());
                }
            } else if (tool == "execute_file") {
                std::string fp = arg_str(action, "file_path");
                if (!fp.empty()) {
                    goals.push_back("Execute " +
                                    fs::path(fp).filename().string());
                }
            } else if (tool == "run_shell_command") {
                std::string cmd = arg_str(action, "command");
                if (!cmd.empty()) {
                    std::string short_cmd = cmd.substr(0, 60);
                    std::replace(short_cmd.begin(), short_cmd.end(), '\n', ' ');
                    goals.push_back("Run command: " + short_cmd);
                }
            } else if (tool == "finish") {
                finish_seen = true;
            }
        }
        if (finish_seen) {
            goals.push_back("Validate outputs and finish");
        }
        std::vector<std::string> unique;
        std::set<std::string> seen_goals;
        for (auto &goal : goals) {
            if (seen_goals.insert(goal).second) {
                unique.push_back(goal);
            }
        }
        if (unique.size() > 5) {
            unique.resize(5);
        }
        return unique;
    }
private:
    LLMBackendManager &llm;
};

class DependencyPlanner {
public:
    explicit DependencyPlanner(LLMBackendManager &llm) : llm(llm) {}
    std::vector<json> generate_plan(const std::string &task,
                                    const std::vector<std::string> &expected_outputs) {
        if (expected_outputs.empty()) return {};
        std::string tools_desc =
            "- write_file: file_path, content\n"
            "- read_file: file_path\n"
            "- execute_file: file_path, stdin\n"
            "- compile_cpp: file_path\n"
            "- run_shell_command: command\n"
            "- make_directory: dir_path\n"
            "- finish: none\n";
        std::string prompt =
            "Task: " + task + "\n"
            "Required output files: " + join(expected_outputs, ", ") + "\n\n"
            "Available tools:\n" + tools_desc + "\n"
            "Create a JSON plan with a list of actions that will accomplish the task. "
            "Always create directories before writing files, and execute scripts that produce output files.\n"
            "Output ONLY the JSON array of actions.";
        json schema = {{"actions", json::array({
            {{"tool", "write_file (or make_directory, etc.)"},
             {"args", {{"any key", "value"}}}}
        })}};
        json plan = llm.structured_chat({{"user", prompt}}, schema, 0.0, 1024);
        std::vector<json> raw = extract_plan_actions(plan);
        std::vector<json> actions;
        actions.reserve(raw.size());
        for (const auto &a : raw) {
            json norm = normalize_action(a);
            if (!norm.is_null() && norm.is_object() && norm.contains("tool"))
                actions.push_back(std::move(norm));
        }
        if (actions.empty()) return fallback_plan(expected_outputs);
        return fix_files(actions, expected_outputs);
    }

private:
    LLMBackendManager &llm;
    static std::string join(const std::vector<std::string> &v, const std::string &sep) {
        std::string out;
        for (size_t i = 0; i < v.size(); ++i) {
            if (i) out += sep;
            out += v[i];
        }
        return out;
    }
    static void collect_actions(const json &node, std::vector<json> &out) {
        if (node.is_array()) {
            for (const auto &child : node)
                collect_actions(child, out);
        } else if (node.is_object()) {
            if (node.contains("tool") && node["tool"].is_string()) {
                out.push_back(node);
            } else {
                for (auto it = node.begin(); it != node.end(); ++it)
                    collect_actions(it.value(), out);
            }
        }
    }
    static std::vector<json> extract_plan_actions(const json &plan) {
        std::vector<json> actions;
        if (plan.is_object()) {
            if (plan.contains("actions"))
                collect_actions(plan["actions"], actions);
            else if (plan.contains("plan"))
                collect_actions(plan["plan"], actions);
            else
                collect_actions(plan, actions);
        } else if (plan.is_array()) {
            collect_actions(plan, actions);
        }
        return actions;
    }
    static json normalize_action(const json &a) {
        if (!a.is_object()) return json();
        json action = a;
        if (!action.contains("tool") || !action["tool"].is_string()) {
            if (action.contains("name") && action["name"].is_string()) {
                action["tool"] = action["name"];
            } else {
                return json();
            }
        }
        if (!action.contains("args") || !action["args"].is_object())
            action["args"] = json::object();
        for (auto &[k, v] : action.items()) {
            if (k != "tool" && k != "args")
                action["args"][k] = v;
        }
        return action;
    }
    std::vector<json> fallback_plan(const std::vector<std::string> &files) {
        std::vector<json> actions;
        std::set<std::string> seen_dirs;
        for (auto &fp : files) {
            fs::path p(fp);
            std::string parent = p.parent_path().string();
            if (!parent.empty() && parent != "." && !seen_dirs.count(parent)) {
                actions.push_back({{"tool", "make_directory"}, {"args", {{"dir_path", parent}}}});
                seen_dirs.insert(parent);
            }
        }
        return actions;
    }
    std::vector<json> fix_files(std::vector<json> actions,
                                const std::vector<std::string> &expected) {
        std::set<std::string> names;
        for (auto &p : expected) names.insert(fs::path(p).filename().string());
        for (auto &act : actions) {
            if (act.value("tool", "") == "write_file") {
                std::string fp = act["args"].value("file_path", "");
                if (!fp.empty() && names.count(fs::path(fp).filename().string())) {
                    for (auto &real : expected) {
                        if (fs::path(real).filename() == fs::path(fp).filename()) {
                            act["args"]["file_path"] = real;
                            break;
                        }
                    }
                }
            }
        }
        return actions;
    }
};
