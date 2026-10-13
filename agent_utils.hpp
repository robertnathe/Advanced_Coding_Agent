//You are Optima, a world-class algorithmic optimization specialist. You combine rigorous analysis, first-principles 
//reasoning, and creative insight to discover high-impact improvements in algorithms.
//Task: Given an original function, the problem it solves, and detailed simulated evaluation results (score, 
//runtime profiles, failure modes, scaling behavior, and key bottlenecks), produce one significant algorithmic 
//improvement that meaningfully advances performance.
//Core Rules: Make exactly one primary conceptual or structural change. Do not combine multiple unrelated optimizations.
//Prioritize asymptotic improvements, better algorithmic paradigms, superior data structures, or mathematical insights over 
//micro-optimizations or constant-factor tweaks. Preserve correctness on all cases the original algorithm solved successfully.
//Keep the exact same function signature and input/output format. Do not add external dependencies.
//Focus on changes that are likely to produce visible gains on the provided evaluation metrics.
//Reasoning Process (perform internally in this order):
//1. Understand the problem and the current algorithm’s core strategy, approach, and theoretical complexity.
//2. Diagnose the dominant performance bottlenecks and failure modes from the evaluation results.
//3. Break the problem down to first principles and identify the single highest-leverage insight or paradigm shift available.
//4. Define the specific improvement and why it directly addresses the core issue.
//Response Format:
//Use exactly these markdown headers:
//1. Problem and Current Approach
//2. Performance Diagnosis
//3. Key Insight
//4. Proposed Improvement
//5. New Implementation
//6. Expected Impact
//Response Format constraints:
//Section 5 must contain only the complete source code of the modified class/function.
//The response is incomplete and invalid if any part of the class or definition is missing.
//Instructions for each section:
//1–4: Be concise, rigorous, and insightful. Avoid fluff.
//5. New Implementation
//Output the COMPLETE corrected class or function definition (every line). 
//No truncation, no ellipsis, no placeholders, no external comments, no markdown code fences.
//The code must be the full, ready-to-use replacement for the original definition.
//CRITICAL: The entire class/definition must appear in full. Partial or truncated code is not acceptable.
//6. Expected Impact: Predict how the new version should improve the evaluation metrics (score, runtime, scalability, etc.) and explain why.
//Style:
//Be decisive, creative, and elegant. Favor non-obvious solutions that feel like genuine algorithmic progress. Write professional, 
//readable code. Maintain a calm, confident, and focused tone. Do not use hype or self-congratulatory language. This prompt is designed 
//for frontier models. Think carefully before proposing an improvement. 
//CRITICAL OUTPUT RULE (must be followed):
//In section 5 you must print the entire corrected class or function definition with zero omissions.
//Never truncate the code. Never use ellipsis or comments that skip parts of the implementation.
//If the class/function is long, still output every single line.
//Failure to output the complete definition is an incorrect response.
//Before finishing, verify that section 5 contains the complete class/definition with no missing methods or lines.
#pragma once
#include "agent_backend.hpp"
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
    static std::string validation_file_signature(const std::string &fp) {
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
               ": " + std::to_string(size);
    }
    static std::string sanitize_json_string_literals(const std::string &text) {
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
                    while (j < text.size() &&
                           (text[j] == ' ' || text[j] == '\n' ||
                            text[j] == '\r' || text[j] == '\t')) {
                        ++j;
                    }
                    if (j == text.size() || text[j] == '}' || text[j] == ']' ||
                        text[j] == ',' || text[j] == ':') {
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
    static std::string repair_json(const std::string &input) {
        std::string out;
        out.reserve(input.size() + 64);
        bool in_string = false;
        bool escape = false;
        for (size_t i = 0; i < input.size(); ++i) {
            char c = input[i];
            if (in_string) {
                if (escape) {
                    out += c;
                    escape = false;
                } else if (c == '\\') {
                    out += c;
                    escape = true;
                } else if (c == '"') {
                    out += c;
                    in_string = false;
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
                } else if (c == '\'') {
                    out += '"';
                } else {
                    out += c;
                }
            }
        }
        std::string no_trailing;
        no_trailing.reserve(out.size());
        in_string = false;
        escape = false;
        for (size_t i = 0; i < out.size(); ++i) {
            char c = out[i];
            if (in_string) {
                if (escape)
                    escape = false;
                else if (c == '\\')
                    escape = true;
                else if (c == '"')
                    in_string = false;
                no_trailing += c;
            } else {
                if (c == '"') {
                    in_string = true;
                    no_trailing += c;
                } else if (c == ',') {
                    size_t j = i + 1;
                    while (j < out.size() &&
                           std::isspace((unsigned char)out[j]))
                        j++;
                    if (j < out.size() &&
                        (out[j] == '}' || out[j] == ']')) {
                        continue;
                    }
                    no_trailing += c;
                } else {
                    no_trailing += c;
                }
            }
        }
        std::string fixed;
        fixed.reserve(no_trailing.size());
        in_string = false;
        escape = false;
        for (size_t i = 0; i < no_trailing.size(); ++i) {
            char c = no_trailing[i];
            if (in_string) {
                if (escape)
                    escape = false;
                else if (c == '\\')
                    escape = true;
                else if (c == '"')
                    in_string = false;
                fixed += c;
            } else {
                if (c == '"') {
                    in_string = true;
                    fixed += c;
                } else if (c == '{' || c == ',') {
                    fixed += c;
                    size_t j = i + 1;
                    while (j < no_trailing.size() &&
                           std::isspace((unsigned char)no_trailing[j]))
                        j++;
                    if (j < no_trailing.size() &&
                        no_trailing[j] != '"' && no_trailing[j] != '}' &&
                        no_trailing[j] != ']') {
                        size_t k = j;
                        while (k < no_trailing.size() &&
                               (std::isalnum((unsigned char)no_trailing[k]) ||
                                no_trailing[k] == '_'))
                            k++;
                        size_t l = k;
                        while (l < no_trailing.size() &&
                               std::isspace((unsigned char)no_trailing[l]))
                            l++;
                        if (l < no_trailing.size() &&
                            no_trailing[l] == ':') {
                            fixed += '"';
                            fixed += no_trailing.substr(j, k - j);
                            fixed += '"';
                            i = k - 1;
                            continue;
                        }
                    }
                } else {
                    fixed += c;
                }
            }
        }
        int brace_count = 0;
        int bracket_count = 0;
        in_string = false;
        escape = false;
        for (char c : fixed) {
            if (in_string) {
                if (escape)
                    escape = false;
                else if (c == '\\')
                    escape = true;
                else if (c == '"')
                    in_string = false;
            } else {
                if (c == '"')
                    in_string = true;
                else if (c == '{')
                    brace_count++;
                else if (c == '}')
                    brace_count--;
                else if (c == '[')
                    bracket_count++;
                else if (c == ']')
                    bracket_count--;
            }
        }
        while (bracket_count > 0) {
            fixed += ']';
            bracket_count--;
        }
        while (brace_count > 0) {
            fixed += '}';
            brace_count--;
        }
        return fixed;
    }
    static void extract_tools_from_json(const json &j,
                                        std::vector<json> &actions) {
        if (j.is_object()) {
            if (j.contains("failed_generation") &&
                j["failed_generation"].is_string()) {
                try {
                    json fg =
                        json::parse(j["failed_generation"].get<std::string>());
                    extract_tools_from_json(fg, actions);
                } catch (...) {
                    try {
                        std::string sanitized = sanitize_json_string_literals(
                            j["failed_generation"].get<std::string>());
                        json fg = json::parse(sanitized);
                        extract_tools_from_json(fg, actions);
                    } catch (...) {
                    }
                }
            }
            if (j.contains("error") && j["error"].is_object()) {
                extract_tools_from_json(j["error"], actions);
            }
            if (j.contains("tool") &&
                (j["tool"].is_string() || j["tool"].is_object())) {
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
                                sanitize_json_string_literals(
                                    args.get<std::string>());
                            args = json::parse(sanitized);
                        } catch (...) {
                            args = json::object();
                        }
                    }
                }
                if (args.is_object() && args.contains("tool") &&
                    args.contains("args")) {
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
                                sanitize_json_string_literals(
                                    args.get<std::string>());
                            args = json::parse(sanitized);
                        } catch (...) {
                            args = json::object();
                        }
                    }
                }
                if (args.is_object() && args.contains("tool") &&
                    args.contains("args")) {
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
            for (const auto &item : j) {
                extract_tools_from_json(item, actions);
            }
        }
    }
    static std::vector<json> extract_actions_robust(const std::string &text) {
        std::vector<json> actions;
        auto add_action_unique = [&](const json &act) {
            for (const auto &existing : actions) {
                if (existing == act) {
                    return;
                }
            }
            actions.push_back(act);
        };
        auto try_parse_and_extract = [&](const std::string &candidate) {
            std::vector<json> extracted;
            json root = json::parse(candidate, nullptr, false);
            if (!root.is_discarded()) {
                extract_tools_from_json(root, extracted);
            } else {
                std::string sanitized =
                    sanitize_json_string_literals(candidate);
                json s_root = json::parse(sanitized, nullptr, false);
                if (!s_root.is_discarded()) {
                    extract_tools_from_json(s_root, extracted);
                } else {
                    std::string repaired = repair_json(candidate);
                    json r_root = json::parse(repaired, nullptr, false);
                    if (!r_root.is_discarded()) {
                        extract_tools_from_json(r_root, extracted);
                    }
                }
            }
            for (const auto &act : extracted) {
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
                            if (depth == 0 &&
                                start_idx != std::string::npos) {
                                std::string block =
                                    text.substr(start_idx, i - start_idx + 1);
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
    static std::string repair_string_newlines_impl(const std::string &src,
                                                   bool support_triple) {
        std::string out;
        out.reserve(src.size() * 2);
        size_t i = 0;
        int in_str = 0;
        bool escaped = false;
        while (i < src.size()) {
            const char c = src[i];
            if (support_triple && in_str == 3) {
                if (i + 2 < src.size() &&
                    src.compare(i, 3, "\"\"\"") == 0) {
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
    static std::string repair_cpp_string_newlines(const std::string &src) {
        return repair_string_newlines_impl(src, false);
    }
    static std::string repair_python_string_newlines(const std::string &src) {
        return repair_string_newlines_impl(src, true);
    }
    static std::string system_prompt() {
        return
            "You are a Senior AI Coding Agent specializing in algorithmic "
            "optimization.\n"
            "Rules:\n"
            "1. Communicate exclusively through JSON tool calls.\n"
            "2. Format: {\"tool\": \"name\", \"args\": {\"arg\": \"val\"}}\n"
            "3. Return exactly ONE JSON object. Do NOT output multiple JSON "
            "actions in a single response.\n"
            "4. Available tools (use exactly these argument names):\n"
            "   - write_file: file_path, content\n"
            "   - read_file: file_path\n"
            "   - execute_file: file_path, stdin\n"
            "   - compile_cpp: file_path\n"
            "   - run_shell_command: command\n"
            "   - make_directory: dir_path\n"
            "   - finish: (no args)\n"
            "5. For file creation tasks: write_file → execute_file → "
            "verify → finish.\n"
            "6. NEVER call finish until you have EXECUTED any script that "
            "creates output files.\n"
            "7. If validation fails, read the feedback and fix the exact "
            "issue mentioned.\n"
            "8. Always implement complete, functional code without "
            "placeholders or TODO.\n"
            "9. Pay close attention to VALIDATION REQUIREMENTS.\n"
            "10. CRITICAL: Python multi-line strings MUST use triple quotes "
            "or explicit \\n escapes.\n"
            "11. CRITICAL: Inside JSON string values, escape all double "
            "quotes with backslash.\n"
            "12. CRITICAL: Write files to the EXACT paths specified in "
            "EXPECTED OUTPUT FILES.\n"
            "13. CRITICAL: For C++ JSON output, use ONLY "
            "<nlohmann/json.hpp>.\n"
            "14. CRITICAL: If a VALIDATION REQUIREMENT lists an 'expected "
            "substring', "
            "your program MUST print that exact substring to stdout during "
            "normal execution. "
            "If it would not naturally appear, add an explicit print "
            "statement, e.g. "
            "print('power') or std::cout << \"CycleError\" << std::endl;. "
            "Comments and source code do NOT count; the substring must "
            "appear in runtime output.\n"
            "15. When a [WorkspaceRecon] block is present in HISTORY, treat "
            "it as ground truth "
            "for existing files and directories; do not invent paths that "
            "do not appear there.\n";
    }
    static json build_tool_schema() {
        auto make_schema = [](const std::string &name, const json &props,
                              const std::vector<std::string> &req) -> json {
            return {{"properties",
                     {{"tool", {{"const", name}}},
                      {"args",
                       {{"type", "object"},
                        {"properties", props},
                        {"required", req},
                        {"additionalProperties", false}}}}},
                    {"required", json::array({"tool", "args"})}};
        };
        json write_file_schema =
            make_schema("write_file",
                        {{"file_path", {{"type", "string"}}},
                         {"content", {{"type", "string"}}}},
                        {"file_path", "content"});
        json read_file_schema =
            make_schema("read_file",
                        {{"file_path", {{"type", "string"}}}}, {"file_path"});
        json execute_file_schema =
            make_schema("execute_file",
                        {{"file_path", {{"type", "string"}}},
                         {"stdin", {{"type", "string"}}}},
                        {"file_path"});
        json compile_cpp_schema =
            make_schema("compile_cpp",
                        {{"file_path", {{"type", "string"}}}}, {"file_path"});
        json run_shell_schema =
            make_schema("run_shell_command",
                        {{"command", {{"type", "string"}}}}, {"command"});
        json make_dir_schema =
            make_schema("make_directory",
                        {{"dir_path", {{"type", "string"}}}}, {"dir_path"});
        json finish_schema = {
            {"properties",
             {{"tool", {{"const", "finish"}}},
              {"args",
               {{"type", "object"}, {"additionalProperties", false}}}}},
            {"required", json::array({"tool", "args"})}};
        return {{"type", "object"},
                {"oneOf", json::array({write_file_schema, read_file_schema,
                                       execute_file_schema, compile_cpp_schema,
                                       run_shell_schema, make_dir_schema,
                                       finish_schema})}};
    }
    static std::string get_error_hint(const std::string &error_output) {
        std::string low = error_output;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        if (low.find("unterminated string") != std::string::npos) {
            return "HINT: Unterminated string literal. Use triple quotes or "
                   "\\n.";
        }
        if (low.find("indentationerror") != std::string::npos) {
            return "HINT: Python indentation error. Use exactly 4 spaces per "
                   "level.";
        }
        if (low.find("modulenotfounderror") != std::string::npos ||
            low.find("no module named") != std::string::npos) {
            return "HINT: Missing Python module. Install it or use standard "
                   "library.";
        }
        if (low.find("no such file") != std::string::npos) {
            return "HINT: File not found. Ensure write_file succeeds before "
                   "execute_file.";
        }
        if (low.find("json/json.h") != std::string::npos) {
            return "HINT: Replace <json/json.h> with <nlohmann/json.hpp>.";
        }
        return "";
    }
    static bool is_critical_error(const std::string &obs) {
        return obs.find("SyntaxError:") != std::string::npos ||
               obs.find("IndentationError:") != std::string::npos ||
               obs.find("ModuleNotFoundError:") != std::string::npos ||
               obs.find("No such file or directory:") != std::string::npos ||
               obs.find("fatal error:") != std::string::npos ||
               obs.find("error: expected") != std::string::npos ||
               obs.find("COMPILATION_FAILED:") != std::string::npos ||
               obs.find("invalid syntax") != std::string::npos;
    }
    static std::string error_type_from_output(const std::string &obs) {
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
    static std::string validation_cache_key(const json &check,
                                            const std::string &fp) {
        std::string input =
            check.contains("input") && check["input"].is_string()
                ? check["input"].get<std::string>()
                : "";
        std::string expect =
            check.contains("expect") && check["expect"].is_string()
                ? check["expect"].get<std::string>()
                : "";
        return fp + "| " + input + "| " + expect;
    }
};
