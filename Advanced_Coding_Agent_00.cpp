#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>
#include <mutex>
#include <functional>
#include <regex>
#include <filesystem>
#include <cstdlib>
#include <ctime>
#include <atomic>
#include <queue>
#include <random>
#include <memory>
#include <stdexcept>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sys/wait.h>
#include <unistd.h>
#include <limits>

using json = nlohmann::json;
namespace fs = std::filesystem;

#include "agent_tools.hpp"
#include "TestSuite.h"

static std::string join(const std::vector<std::string>& vec, const std::string& delim) {
    std::string result;
    for (size_t i = 0; i < vec.size(); ++i) {
        result += vec[i];
        if (i + 1 < vec.size()) {
            result += delim;
        }
    }
    return result;
}

static std::string join_last(const std::vector<std::string>& vec, size_t n) {
    std::string result;
    size_t start = (vec.size() > n) ? vec.size() - n : 0;
    for (size_t i = start; i < vec.size(); ++i) {
        result += vec[i];
        if (i + 1 < vec.size()) {
            result += "\n";
        }
    }
    return result;
}

class CodingAgentUtils {
public:
    static std::string validation_file_signature(const std::string& fp) {
        std::error_code ec;
        auto ftime = fs::last_write_time(fp, ec);
        if (ec) {
            return "missing";
        }
        auto size = fs::file_size(fp, ec);
        if (ec) {
            return "missing";
        }
        return std::to_string(ftime.time_since_epoch().count()) +
               ":" + std::to_string(size);
    }
    static std::string sanitize_json_string_literals(const std::string& text) {
        std::string out;
        out.reserve(text.size() + text.size() / 4);
        bool in_string = false;
        bool escaped = false;
        for (size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (in_string) {
                if (escaped) {
                    out += c;
                    escaped = false;
                } else if (c == '\\') {
                    out += c;
                    escaped = true;
                } else if (c == '"') {
                    bool is_end = false;
                    size_t j = i + 1;
                    while (j < text.size() && (text[j] == ' ' || text[j] == '\n' || text[j] == '\r' || text[j] == '\t')) {
                        ++j;
                    }
                    if (j == text.size() || text[j] == '}' || text[j] == ']' || text[j] == ',' || text[j] == ':') {
                        is_end = true;
                    }
                    if (is_end) {
                        out += c;
                        in_string = false;
                    } else {
                        out += "\\\"";
                    }
                } else if (c == '\n') {
                    out += "\\n";
                } else if (c == '\r') {
                    out += "\\r";
                } else if (c == '\t') {
                    out += "\\t";
                } else {
                    out += c;
                }
            } else {
                if (c == '"') {
                    out += c;
                    in_string = true;
                } else {
                    out += c;
                }
            }
        }
        return out;
    }
    static void extract_tools_from_json(const json& j, std::vector<json>& actions) {
        if (j.is_object()) {
            if (j.contains("failed_generation") && j["failed_generation"].is_string()) {
                try {
                    json fg = json::parse(j["failed_generation"].get<std::string>());
                    extract_tools_from_json(fg, actions);
                } catch (...) {
                    try {
                        std::string sanitized =
                            sanitize_json_string_literals(j["failed_generation"].get<std::string>());
                        json fg = json::parse(sanitized);
                        extract_tools_from_json(fg, actions);
                    } catch (...) {
                    }
                }
            }
            if (j.contains("error") && j["error"].is_object()) {
                extract_tools_from_json(j["error"], actions);
            }
            if (j.contains("tool") && (j["tool"].is_string() || j["tool"].is_object())) {
                actions.push_back(j);
                return;
            }
            if (j.contains("action") && j["action"].is_object() &&
                j["action"].contains("tool")) {
                actions.push_back(j["action"]);
                return;
            }
            if (j.contains("tool_call") && j["tool_call"].is_object() &&
                j["tool_call"].contains("tool")) {
                actions.push_back(j["tool_call"]);
                return;
            }
            if (j.contains("function") && j["function"].is_object() &&
                j["function"].contains("name")) {
                json act;
                act["tool"] = j["function"]["name"].get<std::string>();
                json args = j["function"].value("arguments", json::object());
                if (args.is_string()) {
                    try {
                        args = json::parse(args.get<std::string>());
                    } catch (...) {
                        try {
                            std::string sanitized =
                                sanitize_json_string_literals(args.get<std::string>());
                            args = json::parse(sanitized);
                        } catch (...) {
                            args = json::object();
                        }
                    }
                }
                if (args.is_object() && args.contains("tool") && args.contains("args")) {
                    actions.push_back(args);
                } else {
                    act["args"] = args;
                    actions.push_back(act);
                }
                return;
            }
            if (j.contains("name") && j["name"].is_string() &&
                j.contains("arguments")) {
                json act;
                act["tool"] = j["name"].get<std::string>();
                json args = j["arguments"];
                if (args.is_string()) {
                    try {
                        args = json::parse(args.get<std::string>());
                    } catch (...) {
                        try {
                            std::string sanitized =
                                sanitize_json_string_literals(args.get<std::string>());
                            args = json::parse(sanitized);
                        } catch (...) {
                            args = json::object();
                        }
                    }
                }
                if (args.is_object() && args.contains("tool") && args.contains("args")) {
                    actions.push_back(args);
                } else {
                    act["args"] = args;
                    actions.push_back(act);
                }
                return;
            }
            for (auto it = j.begin(); it != j.end(); ++it) {
                extract_tools_from_json(it.value(), actions);
            }
        } else if (j.is_array()) {
            for (const auto& item : j) {
                extract_tools_from_json(item, actions);
            }
        }
    }
    static std::vector<json> extract_actions_robust(const std::string& text) {
        std::vector<json> actions;
        auto add_action_unique = [&actions](const json& act) {
            for (const auto& existing : actions) {
                if (existing == act) {
                    return;
                }
            }
            actions.push_back(act);
        };
        auto try_parse_and_extract = [&](const std::string& candidate) {
            std::vector<json> extracted;
            json root = json::parse(candidate, nullptr, false);
            if (!root.is_discarded()) {
                extract_tools_from_json(root, extracted);
            } else {
                std::string sanitized = sanitize_json_string_literals(candidate);
                json s_root = json::parse(sanitized, nullptr, false);
                if (!s_root.is_discarded()) {
                    extract_tools_from_json(s_root, extracted);
                }
            }
            for (const auto& act : extracted) {
                add_action_unique(act);
            }
        };
        try_parse_and_extract(text);
        if (!actions.empty()) {
            return actions;
        }
        bool in_string = false;
        bool escape = false;
        bool in_think = false;
        int depth = 0;
        size_t start_idx = std::string::npos;
        std::vector<char> stack;
        for (size_t i = 0; i < text.size(); ++i) {
            if (!in_string) {
                if (!in_think && i + 7 <= text.size() &&
                    text.compare(i, 7, "<think>") == 0) {
                    in_think = true;
                    i += 6;
                    continue;
                }
                if (in_think && i + 8 <= text.size() &&
                    text.compare(i, 8, "</think>") == 0) {
                    in_think = false;
                    i += 7;
                    continue;
                }
            }
            if (in_think) {
                continue;
            }
            const char c = text[i];
            if (escape) {
                escape = false;
                continue;
            }
            if (c == '\\') {
                if (in_string) {
                    escape = true;
                }
                continue;
            }
            if (c == '"') {
                in_string = !in_string;
                continue;
            }
            if (!in_string) {
                if (c == '{' || c == '[') {
                    if (depth == 0) {
                        start_idx = i;
                    }
                    stack.push_back(c);
                    ++depth;
                } else if (c == '}' || c == ']') {
                    if (depth > 0) {
                        const char expected = (c == '}') ? '{' : '[';
                        if (stack.back() == expected) {
                            stack.pop_back();
                            --depth;
                            if (depth == 0 && start_idx != std::string::npos) {
                                std::string block = text.substr(start_idx, i - start_idx + 1);
                                try_parse_and_extract(block);
                                start_idx = std::string::npos;
                            }
                        } else {
                            stack.clear();
                            depth = 0;
                            start_idx = std::string::npos;
                        }
                    }
                }
            }
        }
        if (depth > 0 && start_idx != std::string::npos) {
            std::string block = text.substr(start_idx);
            if (in_string) {
                block += "\"";
            }
            while (!stack.empty()) {
                block += (stack.back() == '{') ? '}' : ']';
                stack.pop_back();
            }
            try_parse_and_extract(block);
        }
        return actions;
    }
private:
    static std::string repair_string_newlines_impl(const std::string& src,
                                                   bool support_triple) {
        std::string out;
        out.reserve(src.size() * 2);
        size_t i = 0;
        int in_str = 0;
        bool escaped = false;
        while (i < src.size()) {
            const char c = src[i];
            if (support_triple && in_str == 3) {
                if (i + 2 < src.size() && src.compare(i, 3, "\"\"\"") == 0) {
                    out += "\"\"\"";
                    i += 3;
                    in_str = 0;
                    continue;
                }
                out += c;
            } else if (support_triple && in_str == 4) {
                if (i + 2 < src.size() && src.compare(i, 3, "'''") == 0) {
                    out += "'''";
                    i += 3;
                    in_str = 0;
                    continue;
                }
                out += c;
            } else if (in_str != 0) {
                if (escaped) {
                    out += c;
                    escaped = false;
                } else if (c == '\\') {
                    out += c;
                    escaped = true;
                } else if ((in_str == 1 && c == '"') ||
                           (in_str == 2 && c == '\'')) {
                    out += c;
                    in_str = 0;
                } else if (c == '\n') {
                    out += "\\n";
                } else if (c == '\r') {
                    if (i + 1 < src.size() && src[i + 1] == '\n') {
                        out += "\\n";
                        ++i;
                    } else {
                        out += "\\r";
                    }
                } else {
                    out += c;
                }
            } else {
                if (support_triple && c == '"' && i + 2 < src.size() &&
                    src.compare(i, 3, "\"\"\"") == 0) {
                    out += "\"\"\"";
                    i += 3;
                    in_str = 3;
                    continue;
                }
                if (support_triple && c == '\'' && i + 2 < src.size() &&
                    src.compare(i, 3, "'''") == 0) {
                    out += "'''";
                    i += 3;
                    in_str = 4;
                    continue;
                }
                if (c == '"') {
                    out += c;
                    in_str = 1;
                } else if (c == '\'') {
                    out += c;
                    in_str = 2;
                } else {
                    out += c;
                }
            }
            ++i;
        }
        return out;
    }
public:
    static std::string repair_cpp_string_newlines(const std::string& src) {
        return repair_string_newlines_impl(src, false);
    }
    static std::string repair_python_string_newlines(const std::string& src) {
        return repair_string_newlines_impl(src, true);
    }
    static std::string system_prompt() {
        return
            "You are a Senior AI Coding Agent specializing in algorithmic optimization.\n"
            "Rules:\n"
            "1. Communicate exclusively through JSON tool calls.\n"
            "2. Format: {\"tool\": \"name\", \"args\": {\"arg\": \"val\"}}\n"
            "3. Return exactly ONE JSON object. Do NOT output multiple JSON actions in a single response.\n"
            "4. Available tools (use exactly these argument names):\n"
            "   - write_file: file_path, content\n"
            "   - read_file: file_path\n"
            "   - execute_file: file_path, stdin\n"
            "   - compile_cpp: file_path\n"
            "   - run_shell_command: command\n"
            "   - make_directory: dir_path\n"
            "   - finish: (no args)\n"
            "5. For file creation tasks: write_file → execute_file → verify → finish.\n"
            "6. NEVER call finish until you have EXECUTED any script that creates output files.\n"
            "7. If validation fails, read the feedback and fix the exact issue mentioned.\n"
            "8. Always implement complete, functional code without placeholders or TODO.\n"
            "9. Pay close attention to VALIDATION REQUIREMENTS.\n"
            "10. CRITICAL: Python multi-line strings MUST use triple quotes or explicit \\n escapes.\n"
            "11. CRITICAL: Inside JSON string values, escape all double quotes with backslash.\n"
            "12. CRITICAL: Write files to the EXACT paths specified in EXPECTED OUTPUT FILES.\n"
            "13. CRITICAL: For C++ JSON output, use ONLY <nlohmann/json.hpp>.\n"
            "14. CRITICAL: If a VALIDATION REQUIREMENT lists an 'expected substring', "
            "your program MUST print that exact substring to stdout during normal execution. "
            "If it would not naturally appear, add an explicit print statement, e.g. "
            "print('power') or std::cout << \"CycleError\" << std::endl;. "
            "Comments and source code do NOT count; the substring must appear in runtime output.\n"
            "15. When a [WorkspaceRecon] block is present in HISTORY, treat it as ground truth "
            "for existing files and directories; do not invent paths that do not appear there.\n";
    }
    static json build_tool_schema() {
        auto make_schema = [](const std::string& name,
                              const json& props,
                              const std::vector<std::string>& req) -> json {
            return {
                {"properties", {
                    {"tool", {{"const", name}}},
                    {"args", {
                        {"type", "object"},
                        {"properties", props},
                        {"required", req},
                        {"additionalProperties", false}
                    }}
                }},
                {"required", json::array({"tool", "args"})}
            };
        };
        json write_file_schema = make_schema("write_file", {
            {"file_path", {{"type", "string"}}},
            {"content", {{"type", "string"}}}
        }, {"file_path", "content"});
        json read_file_schema = make_schema("read_file", {
            {"file_path", {{"type", "string"}}}
        }, {"file_path"});
        json execute_file_schema = make_schema("execute_file", {
            {"file_path", {{"type", "string"}}},
            {"stdin", {{"type", "string"}}}
        }, {"file_path"});
        json compile_cpp_schema = make_schema("compile_cpp", {
            {"file_path", {{"type", "string"}}}
        }, {"file_path"});
        json run_shell_schema = make_schema("run_shell_command", {
            {"command", {{"type", "string"}}}
        }, {"command"});
        json make_dir_schema = make_schema("make_directory", {
            {"dir_path", {{"type", "string"}}}
        }, {"dir_path"});
        json finish_schema = {
            {"properties", {
                {"tool", {{"const", "finish"}}},
                {"args", {
                    {"type", "object"},
                    {"additionalProperties", false}
                }}
            }},
            {"required", json::array({"tool", "args"})}
        };
        return {
            {"type", "object"},
            {"oneOf", json::array({
                write_file_schema,
                read_file_schema,
                execute_file_schema,
                compile_cpp_schema,
                run_shell_schema,
                make_dir_schema,
                finish_schema
            })}
        };
    }
    static std::string get_error_hint(const std::string& error_output) {
        std::string low = error_output;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        if (low.find("unterminated string") != std::string::npos) {
            return "HINT: Unterminated string literal. Use triple quotes or \\n.";
        }
        if (low.find("indentationerror") != std::string::npos) {
            return "HINT: Python indentation error. Use exactly 4 spaces per level.";
        }
        if (low.find("modulenotfounderror") != std::string::npos ||
            low.find("no module named") != std::string::npos) {
            return "HINT: Missing Python module. Install it or use standard library.";
        }
        if (low.find("no such file") != std::string::npos) {
            return "HINT: File not found. Ensure write_file succeeds before execute_file.";
        }
        if (low.find("json/json.h") != std::string::npos) {
            return "HINT: Replace <json/json.h> with <nlohmann/json.hpp>.";
        }
        return "";
    }
    static bool is_critical_error(const std::string& obs) {
        return obs.find("SyntaxError:") != std::string::npos ||
               obs.find("IndentationError:") != std::string::npos ||
               obs.find("ModuleNotFoundError:") != std::string::npos ||
               obs.find("No such file or directory:") != std::string::npos ||
               obs.find("fatal error:") != std::string::npos ||
               obs.find("error: expected") != std::string::npos ||
               obs.find("COMPILATION_FAILED:") != std::string::npos ||
               obs.find("invalid syntax") != std::string::npos;
    }
    static std::string error_type_from_output(const std::string& obs) {
        std::string low = obs;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        if (low.find("syntaxerror") != std::string::npos ||
            low.find("invalid syntax") != std::string::npos) {
            return "SyntaxError";
        }
        if (low.find("indentationerror") != std::string::npos) {
            return "IndentationError";
        }
        if (low.find("modulenotfound") != std::string::npos) {
            return "ModuleNotFound";
        }
        if (low.find("nameerror") != std::string::npos) {
            return "NameError";
        }
        if (low.find("typeerror") != std::string::npos) {
            return "TypeError";
        }
        if (low.find("compilation_failed") != std::string::npos ||
            low.find("fatal error:") != std::string::npos) {
            return "CompilationError";
        }
        if (low.find("no such file") != std::string::npos) {
            return "FileNotFound";
        }
        return "RuntimeError";
    }
    static std::string validation_cache_key(const json& check,
                                            const std::string& fp) {
        std::string input =
            check.contains("input") && check["input"].is_string()
                ? check["input"].get<std::string>()
                : "";
        std::string expect =
            check.contains("expect") && check["expect"].is_string()
                ? check["expect"].get<std::string>()
                : "";
        return fp + "|" + input + "|" + expect;
    }
};

class CodingAgent {
public:
    CodingAgent()
        : llm(LLMBackendManager::Config{
              //{{"openrouter", "minimax/minimax-m3:free"},
               //{"openrouter", "inclusionai/ling-3.0-flash-sante:free"},
               //{"openrouter", "thinkingmachines/inkling:free"}},
              //{"openrouter"},
              //30}),
              //{{"groq", "llama-3.3-70b-versatile"},
               //{"openrouter", "deepseek/deepseek-chat"},
               //{"google", "gemini-3.5-flash"}},
              //{"groq", "google", "openrouter"},
              //30}),
              {{"groq", "openai/gpt-oss-20b"},
               {"openrouter", "openrouter/free"},
               {"google", "gemini-2.5-flash-lite"}},
              {"openrouter", "groq", "google"},            
              30}),
          decomposer(llm),
          planner(llm) {}
    std::vector<json> extract_actions_robust(const std::string& text) const {
        return CodingAgentUtils::extract_actions_robust(text);
    }
    std::string run(const std::string& task,
                    int max_turns = 6,
                    const std::vector<std::string>& expected_outputs = {},
                    const std::vector<json>& validation_checks = {}) {
        this->expected_outputs = expected_outputs;
        this->validation_checks = validation_checks;
        this->current_task = task;
        this->successful_plan_actions.clear();
        this->last_validation_feedback.clear();
        this->last_raw_error.clear();
        this->executed_action_states_.clear();
        this->executed_action_history_.clear();
        this->validation_cache_.clear();
        this->recovery_execution_cache_.clear();
        this->written_files.clear();
        this->session_context.clear();
        this->semantic_action_counts_.clear();
        this->recent_semantics_.clear();
        this->no_progress_turns_ = 0;
        this->last_progress_ = -1.0;
        this->blocked_action_count_ = 0;
        perform_workspace_recon();
        const std::string task_hash = md5_hash(task);
        bool executed_prior_plan = false;
        std::string cached = semantic_memory.retrieve_similar(task);
        if (!cached.empty()) {
            std::vector<json> plan = extract_replayable_plan(cached);
            if (!plan.empty()) {
                auto [score, missing] = score_plan_against_validation(plan);
                if (score >= 0.4) {
                    std::cout << "[Agent] Replaying cached plan (score=" << score << ")\n";
                    if (score < 0.95) {
                        plan = repair_plan_suffix(plan, missing, task);
                    }
                    execute_plan(plan);
                    executed_prior_plan = true;
                    if (all_validations_pass()) {
                        return succeed(task_hash,
                                       "[Agent] Cached plan satisfied all validations — terminating early. ");
                    }
                    std::vector<std::string> rep_fails = collect_validation_failures();
                    if (!rep_fails.empty()) {
                        last_validation_feedback = join(rep_fails, "\n");
                        session_context.push_back(
                            "[Cached Plan Diagnostics - Fix Required]:\n" + last_validation_feedback);
                    }
                }
            }
        }
        const json tool_call_schema = CodingAgentUtils::build_tool_schema();
        if (!executed_prior_plan) {
            std::cout << "[Agent] Generating plan...\n";
            std::vector<json> plan = planner.generate_plan(task, expected_outputs);
            if (!plan.empty()) {
                auto sub_goals = decomposer.decompose_plan(plan);
                if (!sub_goals.empty()) {
                    std::cout << "[Agent] Plan-derived decomposition into "
                              << sub_goals.size() << " sub-goals.\n";
                    session_context.push_back("Strategic Plan Breakdown: " + join(sub_goals, ", "));
                }
            } else {
                auto sub_goals = decomposer.decompose(task);
                if (sub_goals.size() > 1) {
                    std::cout << "[Agent] Task decomposed into "
                              << sub_goals.size() << " sub-goals.\n";
                    session_context.push_back("Strategic Plan Breakdown: " + join(sub_goals, ", "));
                }
            }
            if (!plan.empty()) {
                execute_plan(plan);
                if (all_validations_pass()) {
                    return succeed(task_hash,
                                   "[Agent] Initial plan execution satisfied all validations — terminating early. ");
                }
                std::vector<std::string> init_fails = collect_validation_failures();
                if (!init_fails.empty()) {
                    std::cout << "[Agent] Closed-loop validation healing for "
                              << init_fails.size() << " failure(s)...\n";
                    const std::string heal_prompt =
                        "TASK: " + task + "\n"
                        "Plan executed but failed validation checks:\n" +
                        join(init_fails, "\n") + "\n" +
                        "Provide the exact fix action to satisfy all checks. "
                        "Return exactly ONE JSON object with 'tool' and 'args'. ";
                    json heal_action = llm.structured_chat(
                        {{"system", CodingAgentUtils::system_prompt()}, {"user", heal_prompt}},
                        tool_call_schema, 0.0, 2048);
                    if (heal_action.is_object() && heal_action.contains("tool")) {
                        dispatch_action(normalize_and_fix_action(heal_action), "Heal");
                        if (all_validations_pass()) {
                            return succeed(task_hash,
                                           "[Agent] Closed-loop validation healing succeeded! ");
                        }
                    }
                    init_fails = collect_validation_failures();
                    if (!init_fails.empty()) {
                        last_validation_feedback = join(init_fails, "\n");
                        session_context.push_back(
                            "[Initial Plan Diagnostics - Fix Required]:\n" + last_validation_feedback);
                    }
                }
            }
        }
        int turn = 0;
        int free_retries = 2;
        int guard = 0;
        const int guard_limit = max_turns * 3 + 6;
        while (turn < max_turns) {
            if (++guard > guard_limit) {
                session_context.push_back("System: iteration guard tripped; exiting loop. ");
                break;
            }
            if (aborted) return "FAILED: Aborted due to timeout or external signal. ";
            if (no_progress_turns_ >= 2) {
                json recovery = synthesize_recovery_action();
                if (!recovery.is_null()) {
                    std::cout << "[Agent] No progress for " << no_progress_turns_
                              << " turns — injecting deterministic recovery action.\n";
                    dispatch_action(recovery, "Recovery");
                    if (all_validations_pass()) {
                        return succeed(task_hash, "[Agent] Deterministic recovery satisfied validations. ");
                    }
                    update_progress();
                    continue;
                }
            }
            const std::string user_msg = build_user_message(task, turn, max_turns);
            std::cout << "\n[Turn " << turn + 1 << "] Querying LLM (structured)...\n";
            std::vector<json> actions;
            json structured_action = llm.structured_chat(
                {{"system", CodingAgentUtils::system_prompt()}, {"user", user_msg}},
                tool_call_schema, 0.0, 4096);
            if (structured_action.is_object() && structured_action.contains("tool")) {
                actions.push_back(structured_action);
                std::cout << "[Turn " << turn + 1 << "] Structured action: "
                          << structured_action.dump().substr(0, 300) << "\n";
            } else {
                std::cout << "[Turn " << turn + 1 << "] Structured empty/invalid, trying raw...\n";
                auto chat_res = llm.chat(
                    {{"system", CodingAgentUtils::system_prompt()}, {"user", user_msg}},
                    0.0, 4096);
                actions = extract_actions_robust(chat_res.content);
            }
            if (actions.empty()) {
                std::cout << "[Turn " << turn + 1 << "] No action parsed.\n";
                session_context.push_back("Error: No valid tool action parsed. ");
                json recovery = synthesize_recovery_action();
                if (!recovery.is_null()) {
                    std::cout << "[Agent] Substituting deterministic recovery for unparsed turn.\n";
                    dispatch_action(recovery, "Recovery");
                    if (all_validations_pass()) {
                        return succeed(task_hash, "[Agent] Recovery action satisfied validations. ");
                    }
                }
                update_progress();
                if (free_retries-- > 0) continue;
                ++turn;
                continue;
            }
            bool finish_called = false;
            for (size_t ai = 0; ai < actions.size(); ++ai) {
                if (aborted) return "FAILED: Aborted due to timeout or external signal. ";
                json action = normalize_and_fix_action(actions[ai]);
                const std::string tool_name = action.value("tool", " ");
                if (tool_name.empty()) continue;
                if (tool_name == "finish") {
                    finish_called = true;
                    break;
                }
                if (should_skip_duplicate_action(action)) {
                    ++blocked_action_count_;
                    std::cout << "[Guard] Blocked duplicate action: " << tool_name << "\n";
                    session_context.push_back(
                        "System Warning: Blocked REDUNDANT call to '" + tool_name +
                        "' on '" + get_action_target_file(action) + "'. Nothing changed since the "
                        "last identical attempt. You MUST pick a DIFFERENT action — if a validation "
                        "substring is missing, rewrite the file so it PRINTS that substring, then "
                        "execute it. ");
                    continue;
                }
                std::cout << "[Turn " << turn + 1 << "." << ai + 1 << "] Executing: "
                          << tool_name << "\n";
                dispatch_action(action, "Turn " + std::to_string(turn + 1) + "." +
                                            std::to_string(ai + 1));
                if (all_validations_pass()) {
                    return succeed(task_hash, "[Agent] Validation passed — terminating early. ");
                }
            }
            if (finish_called) {
                if (all_validations_pass()) {
                    return succeed(task_hash, "[Agent] Finish called and validations pass. ");
                }
                last_validation_feedback = join(collect_validation_failures(), "\n");
                session_context.push_back(
                    "[Validation Diagnostic - Fix Required]:\n" + last_validation_feedback);
            } else {
                std::vector<std::string> fails = collect_validation_failures();
                if (!fails.empty()) last_validation_feedback = join(fails, "\n");
            }
            update_progress();
            ++turn;
        }
        semantic_memory.report_outcome(task_hash, false);
        return "FAILED: Max turns reached without passing all validations. ";
    }
    void abort() { aborted = true; }
    void reset_abort() { aborted = false; }
    SemanticMemoryManager semantic_memory;
    LLMBackendManager llm;
    ToolExecutor tool_executor;
    TaskDecomposer decomposer;
    DependencyPlanner planner;
    std::vector<std::string> session_context;
    std::string last_validation_feedback;
    std::string last_raw_error;
    std::vector<std::string> written_files;
    std::vector<std::string> expected_outputs;
    std::vector<json> validation_checks;
    std::string current_task;
    std::vector<json> successful_plan_actions;
    std::atomic<bool> aborted{false};
    std::unordered_map<std::string, std::string> executed_action_states_;
    std::vector<std::string> executed_action_history_;
    std::unordered_map<std::string, int> semantic_action_counts_;
    std::vector<std::string> recent_semantics_;
    double last_progress_ = -1.0;
    int no_progress_turns_ = 0;
    int blocked_action_count_ = 0;
    struct ValidationResult {
        bool passed;
        std::string output;
    };
    std::unordered_map<std::string, ValidationResult> validation_cache_;
    std::unordered_map<std::string, std::string> recovery_execution_cache_;
    std::string succeed(const std::string& task_hash, const std::string& message) {
        std::cout << message << "\n";
        json plan_json = json::array();
        for (const auto& a : successful_plan_actions) plan_json.push_back(a);
        semantic_memory.store(current_task, plan_json.dump());
        semantic_memory.report_outcome(task_hash, true);
        return "SUCCESS: All expected outputs and validation checks passed. ";
    }
    std::string dispatch_action(const json& action, const std::string& tag) {
        const std::string tool = action.value("tool", " ");
        std::string observation = tool_executor.execute_action_dict(action);
        record_executed_action(action);
        session_context.push_back(tag + ": " + tool + "\n→ " + observation.substr(0, 500));
        successful_plan_actions.push_back(action);
        if (tool == "write_file") {
            const std::string fp = action.value("args", json::object()).value("file_path", " ");
            if (!fp.empty() &&
                std::find(written_files.begin(), written_files.end(), fp) == written_files.end()) {
                written_files.push_back(fp);
            }
            if (observation.find("File written successfully") != std::string::npos) {
                invalidate_validation_cache_for(fp);
                recovery_execution_cache_.clear();
                auto_execute_after_write(fp);
            }
        }
        if (tool == "run_shell_command" || tool == "install_package") {
            validation_cache_.clear();
            recovery_execution_cache_.clear();
        }
        if (CodingAgentUtils::is_critical_error(observation)) {
            last_raw_error = observation;
            std::vector<std::string> fixes;
            if (tool == "compile_cpp" || tool == "execute_file" || tool == "write_file") {
                fixes.push_back("write_file");
            }
            semantic_memory.store_failure(
                md5_hash(current_task),
                CodingAgentUtils::error_type_from_output(observation),
                observation, fixes,
                CodingAgentUtils::get_error_hint(observation));
        }
        return observation;
    }
    void auto_execute_after_write(const std::string& fp) {
        if (fp.empty()) return;
        for (const auto& c : validation_checks) {
            if (c.value("type", " ") != "execution") continue;
            if (c.value("file", " ") != fp) continue;
            const std::string stdin_input = c.value("input", " ");
            const std::string out = execute_cached(fp, stdin_input);
            session_context.push_back(
                "[Auto-Verify] Executed " + fp + " immediately after write.\n→ " +
                out.substr(0, 500));
            const std::string expect = c.value("expect", " ");
            if (!expect.empty()) {
                std::string lo = out, le = expect;
                std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
                std::transform(le.begin(), le.end(), le.begin(), ::tolower);
                if (lo.find(le) == std::string::npos) {
                    session_context.push_back(
                        "[Auto-Verify] MISSING REQUIRED OUTPUT: the program ran but never printed '" +
                        expect + "'. Rewrite " + fp + " so it explicitly prints that exact string "
                        "to stdout at runtime (a comment or identifier does NOT count). ");
                }
            }
            return;
        }
    }
    double progress_ratio() {
        const size_t total = expected_outputs.size() + validation_checks.size();
        if (total == 0) return 1.0;
        size_t ok = 0;
        for (const auto& f : expected_outputs) if (fs::exists(f)) ++ok;
        for (const auto& c : validation_checks) {
            const std::string type = c.value("type", " ");
            if (type == "file_exists") {
                if (fs::exists(c.value("path", " "))) ++ok;
            } else if (type == "execution") {
                const std::string fp = c.value("file", " ");
                if (fs::exists(fp) && get_or_execute_validation(c, fp).passed) ++ok;
            } else {
                ++ok;
            }
        }
        return static_cast<double>(ok) / static_cast<double>(total);
    }
    void update_progress() {
        const double p = progress_ratio();
        if (last_progress_ >= 0.0 && p <= last_progress_) {
            ++no_progress_turns_;
        } else {
            no_progress_turns_ = 0;
        }
        last_progress_ = p;
    }
    json synthesize_recovery_action() {
        for (const auto& c : validation_checks) {
            if (c.value("type", " ") != "execution") continue;
            const std::string fp = c.value("file", " ");
            if (fp.empty() || !fs::exists(fp)) continue;
            json act = {
                {"tool", "execute_file"},
                {"args", {{"file_path", fp}, {"stdin", c.value("input", " ")}}}
            };
            if (!should_skip_duplicate_action(act)) return act;
        }
        for (const auto& f : expected_outputs) {
            if (!fs::exists(f)) continue;
            const std::string ext = fs::path(f).extension().string();
            if (ext != ".cpp" && ext != ".cc" && ext != ".cxx") continue;
            json act = {{"tool", "compile_cpp"}, {"args", {{"file_path", f}}}};
            if (!should_skip_duplicate_action(act)) return act;
        }
        for (const auto& f : expected_outputs) {
            if (!fs::exists(f)) continue;
            json act = {{"tool", "read_file"}, {"args", {{"file_path", f}}}};
            if (!should_skip_duplicate_action(act)) return act;
        }
        std::string stuck_target;
        for (auto it = recent_semantics_.rbegin(); it != recent_semantics_.rend(); ++it) {
            const std::string& sem = *it;
            size_t first_colon = sem.find(':');
            if (first_colon != std::string::npos) {
                size_t second_colon = sem.find(':', first_colon + 1);
                std::string target = (second_colon != std::string::npos)
                    ? sem.substr(first_colon + 1, second_colon - first_colon - 1)
                    : sem.substr(first_colon + 1);
                while (!target.empty() && target.back() == ' ') target.pop_back();
                while (!target.empty() && target.front() == ' ') target.erase(target.begin());
                if (!target.empty() && fs::exists(target)) {
                    stuck_target = target;
                    break;
                }
            }
        }
        if (!stuck_target.empty()) {
            executed_action_states_.erase(stuck_target);
            for (auto it = semantic_action_counts_.begin(); it != semantic_action_counts_.end(); ) {
                if (it->first.find(stuck_target) != std::string::npos) {
                    it = semantic_action_counts_.erase(it);
                } else {
                    ++it;
                }
            }
            no_progress_turns_ = 0;
            blocked_action_count_ = 0;
            return {{"tool", "read_file"}, {"args", {{"file_path", stuck_target}}}};
        }
        return json();
    }
    std::string build_user_message(const std::string& task, int turn, int max_turns) {
        if (!last_raw_error.empty()) {
            const std::string hint = semantic_memory.get_hint_for_error(last_raw_error);
            if (!hint.empty()) session_context.push_back("[FailureHint] " + hint);
            const std::string local = CodingAgentUtils::get_error_hint(last_raw_error);
            if (!local.empty()) session_context.push_back(local);
        }
        std::string repetition_warning;
        if (!executed_action_history_.empty()) {
            repetition_warning =
                "\nREPETITION GUARD: You have already executed " +
                std::to_string(executed_action_history_.size()) +
                " action(s). Do NOT repeat one unless a relevant file changed.\n";
        }
        if (blocked_action_count_ > 0) {
            repetition_warning +=
                "The guard has blocked " + std::to_string(blocked_action_count_) +
                " redundant action(s). Change strategy NOW.\n";
        }
        std::string escalation;
        if (no_progress_turns_ >= 1) {
            escalation =
                "\nESCALATION: " + std::to_string(no_progress_turns_) +
                " consecutive turn(s) made ZERO measurable progress. Stop inspecting files. "
                "Rewrite the failing file end-to-end so it satisfies every validation "
                "requirement verbatim, then execute it.\n";
        }
        const std::string warning = last_validation_feedback.empty()
            ? std::string()
            : "\nVALIDATION FAILED\n" + last_validation_feedback + "\n";
        return "TASK: " + task + "\n" +
               build_file_exists_hint() +
               build_validation_requirements() + "\n" +
               build_progress_hint() + "\n" +
               repetition_warning +
               escalation +
               warning +
               "HISTORY:\n" + join_last(session_context, 8) + "\n" +
               "──────────────────────────────────────\n" +
               "Turn " + std::to_string(turn + 1) + "/" + std::to_string(max_turns) +
               " - Return exactly ONE JSON object with \"tool\" and \"args\". ";
    }
    std::string perform_workspace_recon() {
        std::string recon = "Existing workspace files: ";
        std::vector<std::string> files;
        try {
            for (const auto& entry : fs::directory_iterator(".")) {
                if (entry.is_regular_file()) files.push_back(entry.path().filename().string());
            }
        } catch (...) {}
        recon += files.empty() ? "none" : join(files, ", ");
        session_context.push_back("[WorkspaceRecon] " + recon);
        return recon;
    }
    json normalize_and_fix_action(const json& action) const {
        if (!action.is_object()) return action;
        json fixed = action;
        if (fixed.value("tool", " ") == "write_file" &&
            fixed.contains("args") && fixed["args"].is_object()) {
            const std::string fp = fixed["args"].value("file_path", " ");
            const std::string content = fixed["args"].value("content", " ");
            const std::string ext = fs::path(fp).extension().string();
            if (ext == ".cpp" || ext == ".cc" || ext == ".h" || ext == ".hpp") {
                fixed["args"]["content"] = CodingAgentUtils::repair_cpp_string_newlines(content);
            } else if (ext == ".py") {
                fixed["args"]["content"] = CodingAgentUtils::repair_python_string_newlines(content);
            }
        }
        return fixed;
    }
    std::string build_file_exists_hint() const {
        std::string hint;
        for (const auto& f : expected_outputs) {
            hint += fs::exists(f) ? " [READY] Output exists: " + f + "\n"
                                  : " [MISSING] Expected output: " + f + "\n";
        }
        return hint.empty() ? " " : "FILE STATUS:\n" + hint;
    }
    std::string build_progress_hint() const {
        int present = 0;
        for (const auto& f : expected_outputs) if (fs::exists(f)) ++present;
        return "Progress: " + std::to_string(present) + "/" +
               std::to_string(expected_outputs.size()) + " required files present. ";
    }
    std::string build_validation_requirements() const {
        if (validation_checks.empty()) return " ";
        std::string reqs = "VALIDATION REQUIREMENTS:\n";
        for (size_t i = 0; i < validation_checks.size(); ++i) {
            const auto& c = validation_checks[i];
            const std::string type = c.value("type", " ");
            reqs += std::to_string(i + 1) + ". ";
            if (type == "file_exists") {
                reqs += "File must exist: " + c.value("path", " ") + "\n";
            } else if (type == "execution") {
                reqs += "Execute " + c.value("file", " ");
                const std::string in = c.value("input", " ");
                const std::string exp = c.value("expect", " ");
                if (!in.empty()) reqs += " with stdin: \"" + in + "\"";
                if (!exp.empty()) reqs += " -> stdout MUST CONTAIN the literal text \"" + exp +
                                          "\" (add an explicit print if needed)";
                reqs += "\n";
            }
        }
        return reqs;
    }
    std::vector<std::string> extract_code_dependencies(const std::string& content,
                                                       const std::string& ext) const {
        std::vector<std::string> deps;
        if (ext == ".cpp" || ext == ".cc" || ext == ".h" || ext == ".hpp" || ext == ".cxx") {
            std::regex inc_re(R"(#include\s*["]([^">]+)[<]([^">]+)[">])");
            for (std::sregex_iterator it(content.begin(), content.end(), inc_re), end;
                 it != end; ++it) {
                deps.push_back(fs::path(it->str(1)).stem().string());
            }
        } else if (ext == ".py") {
            std::regex imp_re(R"((?:from\s+([a-zA-Z0-9_]+)\s+import|import\s+([a-zA-Z0-9_]+)))");
            for (std::sregex_iterator it(content.begin(), content.end(), imp_re), end;
                 it != end; ++it) {
                deps.push_back(it->str(1).empty() ? it->str(2) : it->str(1));
            }
        }
        return deps;
    }
    enum class ActionType {
        MAKE_DIR, WRITE_FILE, COMPILE_CPP, EXECUTE_FILE, READ_FILE, BARRIER, FINISH, OTHER
    };
    static int phase_of(ActionType t) {
        switch (t) {
            case ActionType::MAKE_DIR:     return 0;
            case ActionType::WRITE_FILE:   return 1;
            case ActionType::COMPILE_CPP:  return 2;
            case ActionType::BARRIER:
            case ActionType::READ_FILE:
            case ActionType::EXECUTE_FILE: return 3;
            case ActionType::FINISH:       return 4;
            default:                       return 2;
        }
    }
    void execute_plan(const std::vector<json>& raw_plan) {
        if (raw_plan.empty()) return;
        const size_t n = raw_plan.size();
        if (n == 1) {
            json a = normalize_and_fix_action(raw_plan[0]);
            if (a.value("tool", " ") == "finish" || should_skip_duplicate_action(a)) return;
            dispatch_action(a, "Plan Step 1");
            return;
        }
        struct DAGNode {
            size_t id = 0;
            json action;
            ActionType type = ActionType::OTHER;
            std::string target_path, ext, content, command;
            int orig_idx = 0, phase = 2, in_degree = 0;
            std::vector<size_t> dependents;
            bool executed = false, failed = false, blocked = false;
        };
        std::vector<DAGNode> nodes(n);
        std::unordered_map<std::string, std::vector<size_t>> dir_makers, file_writers,
            module_writers, file_compilers, readers;
        std::vector<size_t> barrier_indices, finish_indices;
        for (size_t i = 0; i < n; ++i) {
            nodes[i].id = i;
            nodes[i].orig_idx = static_cast<int>(i);
            nodes[i].action = raw_plan[i];
            const std::string tool = raw_plan[i].value("tool", " ");
            const json args = raw_plan[i].value("args", json::object());
            if (tool == "make_directory") {
                nodes[i].type = ActionType::MAKE_DIR;
                nodes[i].target_path = args.value("dir_path", " ");
                if (!nodes[i].target_path.empty()) dir_makers[nodes[i].target_path].push_back(i);
            } else if (tool == "write_file") {
                nodes[i].type = ActionType::WRITE_FILE;
                nodes[i].target_path = args.value("file_path", " ");
                nodes[i].content = args.value("content", " ");
                if (!nodes[i].target_path.empty()) {
                    file_writers[nodes[i].target_path].push_back(i);
                    nodes[i].ext = fs::path(nodes[i].target_path).extension().string();
                    module_writers[fs::path(nodes[i].target_path).stem().string()].push_back(i);
                }
            } else if (tool == "compile_cpp") {
                nodes[i].type = ActionType::COMPILE_CPP;
                nodes[i].target_path = args.value("file_path", " ");
                if (!nodes[i].target_path.empty()) file_compilers[nodes[i].target_path].push_back(i);
            } else if (tool == "execute_file") {
                nodes[i].type = ActionType::EXECUTE_FILE;
                nodes[i].target_path = args.value("file_path", " ");
                if (!nodes[i].target_path.empty()) readers[nodes[i].target_path].push_back(i);
            } else if (tool == "read_file") {
                nodes[i].type = ActionType::READ_FILE;
                nodes[i].target_path = args.value("file_path", " ");
                if (!nodes[i].target_path.empty()) readers[nodes[i].target_path].push_back(i);
            } else if (tool == "run_shell_command" || tool == "install_package") {
                nodes[i].type = ActionType::BARRIER;
                nodes[i].command = args.value("command", args.value("package", " "));
                barrier_indices.push_back(i);
            } else if (tool == "finish") {
                nodes[i].type = ActionType::FINISH;
                finish_indices.push_back(i);
            }
            nodes[i].phase = phase_of(nodes[i].type);
        }
        std::vector<std::unordered_set<size_t>> adj(n);
        auto add_edge = [&](size_t u, size_t v) {
            if (u != v) adj[u].insert(v);
        };
        for (const auto& [dir, m_idx] : dir_makers) {
            for (const auto& [file, w_idx] : file_writers) {
                if (file.rfind(dir, 0) == 0 && file.size() > dir.size() &&
                    (file[dir.size()] == '/' || file[dir.size()] == '\\')) {
                    for (size_t m : m_idx) for (size_t w : w_idx) add_edge(m, w);
                }
            }
        }
        for (size_t i = 0; i < n; ++i) {
            if (nodes[i].type != ActionType::WRITE_FILE || nodes[i].content.empty()) continue;
            for (const auto& dep : extract_code_dependencies(nodes[i].content, nodes[i].ext)) {
                auto it = module_writers.find(dep);
                if (it == module_writers.end()) continue;
                for (size_t w : it->second) if (w != i) add_edge(w, i);
            }
        }
        for (const auto& [file, comps] : file_compilers) {
            auto it = file_writers.find(file);
            if (it == file_writers.end()) continue;
            for (size_t w : it->second) for (size_t c : comps) add_edge(w, c);
        }
        for (const auto& [file, reads] : readers) {
            auto itw = file_writers.find(file);
            if (itw != file_writers.end())
                for (size_t w : itw->second) for (size_t r : reads) add_edge(w, r);
            auto itc = file_compilers.find(file);
            if (itc != file_compilers.end())
                for (size_t c : itc->second) for (size_t r : reads) add_edge(c, r);
        }
        for (const auto& [path, writers] : file_writers)
            for (size_t k = 0; k + 1 < writers.size(); ++k) add_edge(writers[k], writers[k + 1]);
        for (size_t k = 0; k + 1 < barrier_indices.size(); ++k)
            add_edge(barrier_indices[k], barrier_indices[k + 1]);
        for (size_t b : barrier_indices) {
            for (size_t i = 0; i < b; ++i)
                if (nodes[i].type == ActionType::MAKE_DIR || nodes[i].type == ActionType::WRITE_FILE)
                    add_edge(i, b);
            for (size_t i = b + 1; i < n; ++i)
                if (nodes[i].type == ActionType::EXECUTE_FILE || nodes[i].type == ActionType::COMPILE_CPP)
                    add_edge(b, i);
        }
        for (size_t f : finish_indices)
            for (size_t i = 0; i < n; ++i) if (i != f) add_edge(i, f);
        {
            std::vector<int> color(n, 0);
            std::vector<std::pair<size_t, size_t>> back_edges;
            std::function<void(size_t)> dfs = [&](size_t u) {
                color[u] = 1;
                for (size_t v : adj[u]) {
                    if (color[v] == 1) back_edges.emplace_back(u, v);
                    else if (color[v] == 0) dfs(v);
                }
                color[u] = 2;
            };
            for (size_t i = 0; i < n; ++i) if (color[i] == 0) dfs(i);
            for (const auto& [u, v] : back_edges) adj[u].erase(v);
        }
        for (size_t u = 0; u < n; ++u) {
            for (size_t v : adj[u]) {
                nodes[u].dependents.push_back(v);
                nodes[v].in_degree++;
            }
        }
        struct ReadyNode {
            size_t id;
            int phase;
            int orig_idx;
            bool operator>(const ReadyNode& o) const {
                if (phase != o.phase) return phase > o.phase;
                return orig_idx > o.orig_idx;
            }
        };
        std::priority_queue<ReadyNode, std::vector<ReadyNode>, std::greater<ReadyNode>> pq;
        for (size_t i = 0; i < n; ++i)
            if (nodes[i].in_degree == 0) pq.push({i, nodes[i].phase, nodes[i].orig_idx});
        auto release = [&](DAGNode& node) {
            for (size_t v : node.dependents) {
                if (--nodes[v].in_degree == 0 && !nodes[v].blocked)
                    pq.push({v, nodes[v].phase, nodes[v].orig_idx});
            }
        };
        auto isolate = [&](size_t failed_id) {
            std::queue<size_t> q;
            q.push(failed_id);
            while (!q.empty()) {
                size_t cur = q.front();
                q.pop();
                for (size_t child : nodes[cur].dependents) {
                    if (!nodes[child].blocked) {
                        nodes[child].blocked = true;
                        q.push(child);
                    }
                }
            }
        };
        const json tool_call_schema = CodingAgentUtils::build_tool_schema();
        while (!pq.empty()) {
            if (aborted) break;
            const ReadyNode curr = pq.top();
            pq.pop();
            DAGNode& node = nodes[curr.id];
            if (node.executed || node.blocked) continue;
            json action = normalize_and_fix_action(node.action);
            const std::string tool = action.value("tool", " ");
            if (tool == "finish") {
                node.executed = true;
                release(node);
                continue;
            }
            if (should_skip_duplicate_action(action)) {
                node.executed = true;
                release(node);
                continue;
            }
            std::cout << " [Plan Step " << node.orig_idx + 1 << "/" << n << "] "
                      << tool << " (DR-DAG)\n";
            std::string obs = dispatch_action(
                action, "Plan Step " + std::to_string(node.orig_idx + 1));
            bool success = !CodingAgentUtils::is_critical_error(obs);
            if (!success && (node.type == ActionType::WRITE_FILE ||
                             node.type == ActionType::COMPILE_CPP ||
                             node.type == ActionType::EXECUTE_FILE)) {
                std::cout << " [DR-DAG In-Situ Healing] Repairing step "
                          << node.orig_idx + 1 << " (" << tool << ")...\n";
                const std::string fix_prompt =
                    "TASK: " + current_task + "\n"
                    "Action failed with error:\n" + obs.substr(0, 800) + "\n"
                    "Failed Action: " + action.dump() + "\n"
                    "Fix the code to completely resolve this error. "
                    "Return ONLY ONE JSON object with 'tool' and 'args'. ";
                json fix_action = llm.structured_chat(
                    {{"system", CodingAgentUtils::system_prompt()}, {"user", fix_prompt}},
                    tool_call_schema, 0.0, 2048);
                if (fix_action.is_object() && fix_action.contains("tool")) {
                    json norm_fix = normalize_and_fix_action(fix_action);
                    const std::string fix_obs = dispatch_action(norm_fix, "In-Situ Repair");
                    if (!CodingAgentUtils::is_critical_error(fix_obs)) {
                        std::cout << " [DR-DAG In-Situ Healing] Healed step "
                                  << node.orig_idx + 1 << "!\n";
                        success = true;
                    }
                }
            }
            if (success) {
                node.executed = true;
                if (all_validations_pass()) {
                    std::cout << "[Agent] DR-DAG dynamic early-exit: all validations satisfied.\n";
                    return;
                }
                release(node);
            } else {
                node.failed = true;
                last_raw_error = obs;
                std::cout << " [DR-DAG Fault Isolation] Isolating failure at step "
                          << node.orig_idx + 1 << "; blocking transitive downstream DAG.\n";
                isolate(curr.id);
            }
        }
    }
    std::vector<json> extract_replayable_plan(const std::string& history_text) const {
        std::vector<json> plan;
        try {
            json parsed = json::parse(history_text);
            if (parsed.is_array()) {
                for (const auto& el : parsed) {
                    if (!el.is_object() || !el.contains("tool")) continue;
                    const std::string t = el["tool"].get<std::string>();
                    if (t == "write_file" || t == "compile_cpp" ||
                        t == "execute_file" || t == "make_directory") {
                        plan.push_back(el);
                    }
                }
            }
        } catch (...) {}
        return plan;
    }
    
    std::pair<double, std::vector<std::string>>
    score_plan_against_validation(const std::vector<json>& plan) const {
        std::unordered_set<std::string> produced;
        std::unordered_map<std::string, std::string> file_contents;
        for (const auto& a : plan) {
            if (a.value("tool", "") != "write_file") continue;
            const json& args = a.value("args", json::object());
            const std::string fp = args.value("file_path", "");
            if (!fp.empty()) {
                produced.insert(fp);
                file_contents[fp] = args.value("content", "");
            }
        }
        std::unordered_set<std::string> required;
        for (const auto& f : expected_outputs) if (!f.empty()) required.insert(f);
        for (const auto& c : validation_checks) {
            const std::string t = c.value("type", "");
            const std::string p = (t == "file_exists") ? c.value("path", "")
                : (t == "execution") ? c.value("file", "")
                : std::string();
            if (!p.empty()) required.insert(p);
        }
        if (required.empty()) return {1.0, {}};
        std::vector<std::string> missing;
        for (const auto& r : required) if (!produced.count(r)) missing.push_back(r);
        int content_checks = 0;
        int content_passes = 0;
        for (const auto& c : validation_checks) {
            if (c.value("type", "") != "execution") continue;
            const std::string exp = c.value("expect", "");
            if (exp.empty()) continue;
            const std::string fp = c.value("file", "");
            content_checks++;
            auto it = file_contents.find(fp);
            if (it != file_contents.end()) {
                std::string lo_content = it->second;
                std::string lo_exp = exp;
                std::transform(lo_content.begin(), lo_content.end(), lo_content.begin(), ::tolower);
                std::transform(lo_exp.begin(), lo_exp.end(), lo_exp.begin(), ::tolower);
                if (lo_content.find(lo_exp) != std::string::npos) {
                    content_passes++;
                }
            }
        }
        double file_score = 1.0 - static_cast<double>(missing.size()) / static_cast<double>(required.size());
        double content_score = (content_checks > 0)
            ? static_cast<double>(content_passes) / content_checks
            : 1.0;
        double score = 0.6 * std::max(0.0, file_score) + 0.4 * content_score;
        return {std::max(0.0, score), missing};
    }
    std::vector<json> repair_plan_suffix(const std::vector<json>& base_plan,
                                         const std::vector<std::string>& missing_files,
                                         const std::string& task) {
        if (missing_files.empty()) return base_plan;
        const std::string prompt =
            "Task: " + task + "\n"
            "The following required files are missing from the current plan:\n" +
            join(missing_files, ", ") + "\n\n"
            "Generate ONLY the additional actions needed to create these missing files. ";
        const json schema = {
            {"type", "object"},
            {"properties", {
                {"actions", {
                    {"type", "array"},
                    {"items", {
                        {"type", "object"},
                        {"properties", {
                            {"tool", {{"type", "string"}}},
                            {"args", {
                                {"type", "object"},
                                {"properties", {
                                    {"file_path", {{"type", "string"}}},
                                    {"content", {{"type", "string"}}}
                                }},
                                {"required", json::array({"file_path", "content"})}
                            }}
                        }},
                        {"required", json::array({"tool", "args"})}
                    }}
                }}
            }},
            {"required", json::array({"actions"})}
        };
        json resp = llm.structured_chat({{"user", prompt}}, schema, 0.0, 2048);
        std::vector<json> repaired = base_plan;
        if (resp.is_object() && resp.contains("actions") && resp["actions"].is_array()) {
            for (const auto& a : resp["actions"]) repaired.push_back(a);
        } else if (resp.is_array()) {
            for (const auto& a : resp) repaired.push_back(a);
        }
        return repaired;
    }
    std::string workspace_state_signature() const {
        std::string sig;
        for (const auto& f : written_files)
            sig += f + ": " + CodingAgentUtils::validation_file_signature(f) + "; ";
        return sig;
    }
    std::string validation_cache_key(const json& check, const std::string& fp) const {
        return fp + '\x1f' + check.value("input", " ") + '\x1f' +
               check.value("expect", " ") + '\x1f' + workspace_state_signature();
    }
    std::string execute_cached(const std::string& file_path, const std::string& stdin_input) {
        const std::string ws_sig = workspace_state_signature();
        const std::string exec_key = file_path + '\x1f' + ws_sig + '\x1f' + stdin_input;
        if (auto it = recovery_execution_cache_.find(exec_key);
            it != recovery_execution_cache_.end()) {
            return it->second;
        }
        std::string executable = file_path;
        const std::string ext = fs::path(file_path).extension().string();
        if (ext == ".cpp" || ext == ".cc" || ext == ".cxx") {
            const std::string comp_key =
                "COMP:" + file_path + '\x1f' +
                CodingAgentUtils::validation_file_signature(file_path);
            if (auto c = recovery_execution_cache_.find(comp_key);
                c != recovery_execution_cache_.end()) {
                executable = c->second;
            } else {
                executable = tool_executor.executor.compile_cpp(file_path);
                recovery_execution_cache_[comp_key] = executable;
            }
            if (executable.find("COMPILATION_FAILED:") != std::string::npos) {
                recovery_execution_cache_[exec_key] = executable;
                return executable;
            }
        }
        std::string result = tool_executor.executor.execute_file(executable, stdin_input);
        recovery_execution_cache_[exec_key] = result;
        return result;
    }
    ValidationResult get_or_execute_validation(const json& check, const std::string& fp) {
        const std::string key = validation_cache_key(check, fp);
        if (auto it = validation_cache_.find(key); it != validation_cache_.end()) return it->second;
        const std::string out = execute_cached(fp, check.value("input", " "));
        const std::string exp = check.value("expect", " ");
        bool ok = out.find("Return code: 0") != std::string::npos &&
                  out.find("COMPILATION_FAILED") == std::string::npos;
        if (ok && !exp.empty()) {
            std::string lo = out, le = exp;
            std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
            std::transform(le.begin(), le.end(), le.begin(), ::tolower);
            ok = lo.find(le) != std::string::npos;
        }
        ValidationResult vr{ok, out};
        validation_cache_[key] = vr;
        return vr;
    }
    bool all_validations_pass() {
        if (expected_outputs.empty() && validation_checks.empty()) return true;
        for (const auto& f : expected_outputs) if (!fs::exists(f)) return false;
        for (const auto& check : validation_checks) {
            const std::string type = check.value("type", " ");
            if (type == "file_exists") {
                if (!fs::exists(check.value("path", " "))) return false;
            } else if (type == "execution") {
                const std::string fp = check.value("file", " ");
                if (!fs::exists(fp)) return false;
                if (!get_or_execute_validation(check, fp).passed) return false;
            }
        }
        return true;
    }
    std::vector<std::string> collect_validation_failures() {
        std::vector<std::string> fails;
        for (const auto& f : expected_outputs)
            if (!fs::exists(f)) fails.push_back("Expected output file does not exist: " + f);
        for (const auto& check : validation_checks) {
            const std::string type = check.value("type", " ");
            if (type == "file_exists") {
                const std::string p = check.value("path", " ");
                if (!fs::exists(p)) fails.push_back("Validation failed: file not found: " + p);
            } else if (type == "execution") {
                const std::string fp = check.value("file", " ");
                if (!fs::exists(fp)) {
                    fails.push_back("Validation execution file missing: " + fp);
                    continue;
                }
                const ValidationResult vr = get_or_execute_validation(check, fp);
                if (vr.passed) continue;
                const std::string exp = check.value("expect", " ");
                std::string msg = "Execution check FAILED for " + fp + ". ";
                if (!exp.empty()) {
                    msg += "\n  REQUIRED: stdout must contain the literal text '" + exp + "'. ";
                    msg += "\n  REMEDY: add an explicit statement that PRINTS '" + exp +
                           "' at runtime. Source comments/identifiers do NOT satisfy this. ";
                }
                msg += "\n  ACTUAL OUTPUT:\n" + vr.output.substr(0, 400);
                const std::string hint = CodingAgentUtils::get_error_hint(vr.output);
                if (!hint.empty()) msg += "\n   " + hint;
                fails.push_back(msg);
            }
        }
        return fails;
    }
    void invalidate_validation_cache_for(const std::string& fp) {
        if (fp.empty()) {
            validation_cache_.clear();
            return;
        }
        for (auto it = validation_cache_.begin(); it != validation_cache_.end();) {
            it = (it->first.find(fp) != std::string::npos) ? validation_cache_.erase(it)
                                                           : std::next(it);
        }
    }
    static std::string read_file_text(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return {};
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }
    std::string get_action_target_file(const json& action) const {
        if (!action.is_object()) return " ";
        const std::string tool = action.value("tool", " ");
        const json args = action.value("args", json::object());
        if (tool == "write_file" || tool == "read_file" ||
            tool == "execute_file" || tool == "compile_cpp") {
            return args.value("file_path", " ");
        }
        if (tool == "make_directory") return args.value("dir_path", " ");
        return " ";
    }
    std::string semantic_fingerprint(const json& action) const {
        if (!action.is_object()) return " ";
        const std::string tool = action.value("tool", " ");
        const json args = action.value("args", json::object());
        std::string fp = tool;
        if (tool == "write_file") {
            fp += ": " + args.value("file_path", " ") + ": " + md5_hash(args.value("content", " "));
        } else if (tool == "read_file" || tool == "compile_cpp") {
            fp += ": " + args.value("file_path", " ");
        } else if (tool == "execute_file") {
            fp += ": " + args.value("file_path", " ") + ": " + md5_hash(args.value("stdin", " "));
        } else if (tool == "run_shell_command") {
            fp += ": " + args.value("command", " ");
        } else if (tool == "make_directory") {
            fp += ": " + args.value("dir_path", " ");
        } else if (tool == "install_package") {
            fp += ": " + args.value("package", " ");
        }
        return fp;
    }
    std::string fingerprint_action_stateful(const json& action) const {
        const std::string base = semantic_fingerprint(action);
        const std::string target = get_action_target_file(action);
        if (target.empty()) return base;
        return base + "|state:" + CodingAgentUtils::validation_file_signature(target);
    }
    bool detect_ping_pong(const std::string& candidate) const {
        if (no_progress_turns_ < 1) return false;
        const size_t sz = recent_semantics_.size();
        if (sz < 2) return false;
        return recent_semantics_[sz - 2] == candidate &&
               recent_semantics_[sz - 1] != candidate;
    }
    bool should_skip_duplicate_action(const json& action) const {
        const std::string tool = action.value("tool", " ");
        if (tool == "finish") return false;
        const std::string sem = semantic_fingerprint(action);
        if (sem.empty()) return false;
        if (tool == "write_file") {
            const json args = action.value("args", json::object());
            const std::string fp = args.value("file_path", " ");
            if (!fp.empty() && fs::exists(fp)) {
                if (md5_hash(read_file_text(fp)) == md5_hash(args.value("content", " "))) return true;
            }
        }
        if (auto it = semantic_action_counts_.find(sem);
            it != semantic_action_counts_.end() && it->second >= 2) {
            return true;
        }
        if (!executed_action_history_.empty() &&
            executed_action_history_.back() == fingerprint_action_stateful(action)) {
            return true;
        }
        if (tool == "read_file" || tool == "execute_file" || tool == "compile_cpp") {
            const std::string target = get_action_target_file(action);
            auto st = executed_action_states_.find(target);
            if (st != executed_action_states_.end() &&
                st->second == CodingAgentUtils::validation_file_signature(target) &&
                semantic_action_counts_.count(sem)) {
                return true;
            }
        }
        return detect_ping_pong(sem);
    }
    void record_executed_action(const json& action) {
        const std::string sem = semantic_fingerprint(action);
        if (sem.empty()) return;
        semantic_action_counts_[sem]++;
        recent_semantics_.push_back(sem);
        if (recent_semantics_.size() > 16) {
            recent_semantics_.erase(recent_semantics_.begin());
        }
        executed_action_history_.push_back(fingerprint_action_stateful(action));
        const std::string target = get_action_target_file(action);
        if (!target.empty()) {
            executed_action_states_[target] =
                CodingAgentUtils::validation_file_signature(target);
        }
    }
};

struct EpisodeMetrics {
    bool success = false;
    int turns_used = 0;
    int max_turns = 6;
    int llm_calls = 0;
    int tool_calls = 0;
    int duplicate_actions_blocked = 0;
    int critical_errors = 0;
    int non_zero_executions = 0;
    int missing_expected_files = 0;
    int validation_failures = 0;
    int memory_hits = 0;
    int api_calls_saved = 0;
    double plan_score = 0.0;
    double wall_time_sec = 0.0;
};

double compute_reward(const EpisodeMetrics& m) {
    double r = m.success ? 10.0 : 0.0;
    if (m.success) {
        double turn_efficiency = 1.0 - static_cast<double>(m.turns_used) / m.max_turns;
        double call_efficiency = 1.0 / (1.0 + 0.15 * m.llm_calls);
        r += 3.0 * turn_efficiency + 2.0 * call_efficiency;
    } else {
        r -= 1.5 * (static_cast<double>(m.turns_used) / m.max_turns);
    }
    double progress = 0.0;
    progress += 0.8 * (1.0 - std::min(1.0, m.missing_expected_files / 5.0));
    progress += 0.6 * m.plan_score;
    progress -= 0.4 * m.validation_failures;
    progress -= 0.3 * m.non_zero_executions;
    r += std::clamp(progress, -2.0, 3.0);
    r -= 1.5 * m.critical_errors;
    r -= 0.4 * m.duplicate_actions_blocked;
    if (m.memory_hits > 0 && m.success)
        r += 1.5 + 0.3 * std::min(m.api_calls_saved, 5);
    else if (m.memory_hits > 0 && !m.success)
        r -= 0.5;
    if (m.wall_time_sec > 120.0)
        r -= 0.01 * (m.wall_time_sec - 120.0);
    return std::clamp(r, -8.0, 18.0);
}

static void run_single_task(CodingAgent &agent) {
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    std::cout << "Enter task description: ";
    std::string task;
    std::getline(std::cin, task);
    if (task.empty()) return;
    std::cout << "Expected output files (comma-separated, optional): ";
    std::string expected_line;
    std::getline(std::cin, expected_line);
    std::vector<std::string> expected_outputs;
    size_t pos = 0;
    while ((pos = expected_line.find(',')) != std::string::npos) {
        std::string f = expected_line.substr(0, pos);
        if (!f.empty()) expected_outputs.push_back(f);
        expected_line.erase(0, pos + 1);
    }
    if (!expected_line.empty()) expected_outputs.push_back(expected_line);
    std::vector<json> checks;
    if (!expected_outputs.empty()) {
        std::cout << "Add validation checks? (y/n): ";
        std::string add;
        std::getline(std::cin, add);
        if (add == "y" || add == "yes") {
            for (auto &fp : expected_outputs) {
                if (fp.ends_with(".py") || fp.ends_with(".cpp")) {
                    std::cout << "Expected output substring for " << fp << " (optional): ";
                    std::string expect;
                    std::getline(std::cin, expect);
                    std::cout << "Stdin input for " << fp << " (optional): ";
                    std::string stdin_str;
                    std::getline(std::cin, stdin_str);
                    json check = {{"type", "execution"}, {"file", fp}};
                    if (!expect.empty()) check["expect"] = expect;
                    if (!stdin_str.empty()) check["input"] = stdin_str;
                    checks.push_back(check);
                } else {
                    checks.push_back({{"type", "file_exists"}, {"path", fp}});
                }
            }
        }
    }
    std::cout << "Max turns (default 6): ";
    std::string mt;
    std::getline(std::cin, mt);
    int max_turns = mt.empty() ? 6 : std::stoi(mt);
    std::string result = agent.run(task, max_turns, expected_outputs, checks);
    std::cout << "\nResult: " << result << "\n";
}

int main() {
    curl_global_init(CURL_GLOBAL_ALL);
    srand(static_cast<unsigned int>(time(nullptr)));
    try {
        CodingAgent agent;
        TestSuite suite;
        while (true) {
            std::cout << "\n==================================================\n";
            std::cout << "1. Single task\n";
            std::cout << "2. Run tests\n";
            std::cout << "3. Stats\n";
            std::cout << "4. Quit\n";
            std::cout << "Choice: ";
            std::string choice;
            std::getline(std::cin, choice);
            if (choice == "4" || choice == "quit" || choice == "exit") {
                break;
            } else if (choice == "1") {
                run_single_task(agent);
            } else if (choice == "2") {
                std::cout << "Test IDs to run (comma-separated, or 'all'): ";
                std::string ids;
                std::getline(std::cin, ids);
                std::vector<std::string> test_ids;
                if (!ids.empty() && ids != "all") {
                    size_t pos = 0;
                    while ((pos = ids.find(',')) != std::string::npos) {
                        test_ids.push_back(ids.substr(0, pos));
                        ids.erase(0, pos + 1);
                    }
                    if (!ids.empty()) test_ids.push_back(ids);
                }
                int total_tests = 0;
                int passed_tests = 0;
                for (auto &tc : suite.get_cases()) {
                    if (!test_ids.empty() &&
                        std::find(test_ids.begin(), test_ids.end(), tc.id) == test_ids.end())
                        continue;
                    ++total_tests;
                    std::cout << "\n--- " << tc.id << " " << tc.name << " ---\n";
                    agent.reset_abort();
                    auto start = std::chrono::steady_clock::now();
                    std::string output;
                    std::atomic<bool> done{false};
                    std::exception_ptr exc = nullptr;
                    std::thread t([&] {
                        try {
                            output = agent.run(tc.task_prompt, tc.max_turns,
                                               tc.expected_outputs, tc.validation_checks);
                        } catch (...) {
                            exc = std::current_exception();
                        }
                        done = true;
                    });
                    auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(tc.timeout);
                    while (!done && std::chrono::steady_clock::now() < deadline) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                    double dur = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - start).count();
                    if (!done) {
                        std::cout << "✗ Timed out after " << tc.timeout << "s\n";
                        agent.abort();
                        t.join();
                    } else {
                        t.join();
                        if (exc) {
                            std::cout << "✗ Exception: ";
                            try {
                                std::rethrow_exception(exc);
                            } catch (const std::exception &e) {
                                std::cout << e.what();
                            } catch (...) {
                                std::cout << "unknown";
                            }
                            std::cout << "\n";
                        } else if (output.find("SUCCESS") != std::string::npos) {
                            std::cout << "✓ Passed in " << dur << "s\n";
                            ++passed_tests;
                        } else {
                            std::cout << "✗ Failed in " << dur << "s\n";
                        }
                    }
                }
                if (total_tests > 0) {
                    double pass_rate = 100.0 * passed_tests / total_tests;
                    std::cout << "\n================ TEST SUMMARY ================\n";
                    std::cout << "Passed: " << passed_tests << "/" << total_tests << "\n";
                    std::cout << "Pass rate: " << pass_rate << "%\n";
                } else {
                    std::cout << "\nNo matching tests were run.\n";
                }
            } else if (choice == "3") {
                agent.semantic_memory.print_stats();
            } else {
                std::cout << "Invalid choice.\n";
            }
        }
    } catch (const std::exception &e) {
        std::cerr << "Initialization error: " << e.what() << "\n";
        std::cerr << "Make sure at least one API key is set:\n";
        std::cerr << "  GROQ_API_KEY, OPENROUTER_API_KEY, or GOOGLE_API_KEY\n";
        return 1;
    }
    curl_global_cleanup();
    return 0;
}
