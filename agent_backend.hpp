#pragma once
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

enum class ErrorCategory {
    NONE,
    TRANSIENT,
    QUOTA,
    PERMANENT
};

static double get_time() {
    return std::chrono::duration<double>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static std::string md5_hash(const std::string &s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ULL;
    std::ostringstream oss;
    oss << std::hex << (h & 0xFFFFFFFFULL);
    return oss.str();
}

static std::string error_type_from_output(const std::string &output) {
    std::string low = output;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    if (low.find("syntaxerror") != std::string::npos) return "SyntaxError";
    if (low.find("indentationerror") != std::string::npos) return "IndentationError";
    if (low.find("modulenotfounderror") != std::string::npos) return "ModuleNotFoundError";
    if (low.find("compilation failed") != std::string::npos) return "CompilationFailed";
    if (low.find("segmentation fault") != std::string::npos) return "SegmentationFault";
    return "GenericError";
}

static const std::unordered_set<std::string> STOP_WORDS = {
    "the", "and", "is", "in", "to", "of", "a", "for", "on", "with", "as", "by", "at", "an", "be",
    "this", "that", "it", "not", "or", "but", "are", "from", "has", "had", "have", "will", "would",
    "could", "should", "may", "can", "do", "does", "did", "was", "were", "been", "being", "am", "i",
    "you", "he", "she", "we", "they", "me", "him", "her", "us", "them", "my", "your", "his", "its",
    "our", "their", "what", "when", "where", "why", "how", "all", "any", "both", "each", "few", "more",
    "most", "other", "some", "such", "no", "nor", "only", "own", "same", "so", "than", "through",
    "too", "under", "until", "up", "very", "with", "would"
};

static std::vector<std::string> tokenize(const std::string& text) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    static const std::regex re(R"(\b[a-z]{3,}\b)");
    std::sregex_iterator it(lower.begin(), lower.end(), re);
    std::sregex_iterator end;
    std::vector<std::string> tokens;
    for (; it != end; ++it) {
        std::string w = it->str();
        if (!STOP_WORDS.count(w)) {
            tokens.push_back(w);
        }
    }
    return tokens;
}

static std::string extract_json_with_bracket_matching(const std::string & s) {
    int first_obj = -1;
    int last_obj = -1;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
        } else {
            if (c == '"') {
                in_string = true;
            } else if (c == '{') {
                if (depth == 0) {
                    first_obj = static_cast<int>(i);
                }
                depth++;
            } else if (c == '}') {
                if (depth > 0) {
                    depth--;
                    if (depth == 0) {
                        last_obj = static_cast<int>(i);
                    }
                }
            }
        }
    }
    if (first_obj != -1 && last_obj != -1 && last_obj > first_obj) {
        return s.substr(
            first_obj,
            last_obj - first_obj + 1
        );
    }
    return "";
}

static std::string smart_sanitize_for_structured(const std::string &s) {
    std::string res;
    bool in_string = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"' && (i == 0 || s[i-1] != '\\')) {
            in_string = !in_string;
        }
        if (in_string && (c == '\n' || c == '\r')) {
            res += "\\n";
        } else {
            res += c;
        }
    }
    return res;
}

class RateLimiter {
public:
    explicit RateLimiter(int rpm = 60) : interval(60.0 / rpm) {}
    void acquire() {
        std::lock_guard<std::mutex> lk(mtx);
        double now = get_time();
        double sleep_time = last_call + interval - now;
        if (sleep_time > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(
                static_cast<long long>(sleep_time * 1000)));
        }
        last_call = get_time();
    }
private:
    double interval;
    double last_call = 0.0;
    std::mutex mtx;
};

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *out) {
    size_t total = size * nmemb;
    out->append(static_cast<char*>(contents), total);
    return total;
}

static bool http_post_json(const std::string &url,
                           const std::vector<std::string> &header_lines,
                           const json &payload,
                           int timeout_sec,
                           std::string &response,
                           std::string *error_out = nullptr,
                           long *http_code_out = nullptr) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        if (error_out) *error_out = "curl_easy_init() failed";
        return false;
    }
    curl_slist *headers = nullptr;
    for (auto &h : header_lines)
        headers = curl_slist_append(headers, h.c_str());
    std::string body = payload.dump();
    response.clear();
    char curl_errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 6L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code_out) *http_code_out = http_code;
    std::string error;
    if (res != CURLE_OK) {
        error = std::string(curl_errbuf[0] ? curl_errbuf : curl_easy_strerror(res));
        error = "curl error: " + error;
    } else if (http_code != 200) {
        error = "HTTP " + std::to_string(http_code) + ": " + response.substr(0, 300);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (!error.empty()) {
        if (error_out) *error_out = error;
        return false;
    }
    return true;
}

struct ChatResult {
    std::string content;
    bool ok = false;
    std::string error;
    ErrorCategory category = ErrorCategory::NONE;
};

class LLMClientBase {
public:
    explicit LLMClientBase(std::string api_key) : api_key(std::move(api_key)) {}
    virtual ~LLMClientBase() = default;
    virtual ChatResult fetch(const std::string &model,
                             const std::vector<std::pair<std::string, std::string>> &messages,
                             double temperature,
                             int max_tokens,
                             const json &schema = json(),
                             int timeout_sec = 12) = 0;
    static ErrorCategory classify_error(long http_code, const std::string &err) {
        std::string low = err;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        if (http_code == 401 || http_code == 403 || http_code == 404 ||
            low.find("model_not_found") != std::string::npos ||
            low.find("does not exist") != std::string::npos ||
            low.find("invalid_api_key") != std::string::npos) {
            return ErrorCategory::PERMANENT;
        }
        if (http_code == 429 || http_code == 402 ||
            low.find("quota") != std::string::npos ||
            low.find("credits") != std::string::npos ||
            low.find("free-models-per-day") != std::string::npos ||
            low.find("rate limit") != std::string::npos) {
            return ErrorCategory::QUOTA;
        }
        if (http_code == 503 || http_code == 502 || http_code == 504 ||
            low.find("unavailable") != std::string::npos ||
            low.find("high demand") != std::string::npos) {
            return ErrorCategory::TRANSIENT;
        }
        return ErrorCategory::TRANSIENT;
    }
protected:
    std::string api_key;
    static json infer_google_schema_type(const json &value) {
        if (value.is_string()) return {{"type", "STRING"}};
        if (value.is_number()) return {{"type", "NUMBER"}};
        if (value.is_boolean()) return {{"type", "BOOLEAN"}};
        if (value.is_array()) {
            json item_schema = value.empty()
                ? json::object({{"type", "STRING"}})
                : infer_google_schema_type(value[0]);
            return {{"type", "ARRAY"}, {"items", item_schema}};
        }
        if (value.is_object()) {
            json obj;
            obj["type"] = "OBJECT";
            obj["properties"] = json::object();
            for (auto &[k, v] : value.items()) {
                obj["properties"][k] = infer_google_schema_type(v);
            }
            return obj;
        }
        return {{"type", "STRING"}};
    }
    static json to_google_response_schema(const json &schema) {
        if (schema.is_null() || schema.empty()) return json::object();
        if (schema.is_object() && schema.contains("type") && schema.contains("properties")) return schema;
        json result = json::object();
        result["type"] = "OBJECT";
        result["properties"] = json::object();
        if (schema.is_object()) {
            for (auto &[key, value] : schema.items()) {
                result["properties"][key] = infer_google_schema_type(value);
            }
        }
        return result;
    }
    static std::string extract_json_payload(std::string s) {
        auto trim = [](std::string &str) {
            while (!str.empty() && std::isspace(static_cast<unsigned char>(str.front()))) str.erase(str.begin());
            while (!str.empty() && std::isspace(static_cast<unsigned char>(str.back()))) str.pop_back();
        };
        size_t first_fence = s.find("```");
        if (first_fence != std::string::npos) {
            size_t first_nl = s.find('\n', first_fence);
            if (first_nl != std::string::npos) {
                size_t last_fence = s.rfind("```");
                if (last_fence != std::string::npos && last_fence > first_nl) {
                    std::string extracted = s.substr(first_nl + 1, last_fence - first_nl - 1);
                    trim(extracted);
                    if (!extracted.empty() && (extracted.front() == '{' || extracted.front() == '[')) {
                        return extracted;
                    }
                }
            }
        }
        size_t start = std::string::npos;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '{' || s[i] == '[') {
                start = i;
                break;
            }
        }
        if (start != std::string::npos) {
            char open_char = s[start];
            char close_char = (open_char == '{') ? '}' : ']';
            int depth = 0;
            bool in_string = false;
            bool escape = false;
            size_t end = std::string::npos;
            for (size_t i = start; i < s.size(); ++i) {
                if (escape) {
                    escape = false;
                    continue;
                }
                if (s[i] == '\\') {
                    escape = true;
                    continue;
                }
                if (s[i] == '"') {
                    in_string = !in_string;
                    continue;
                }
                if (!in_string) {
                    if (s[i] == open_char) {
                        depth++;
                    } else if (s[i] == close_char) {
                        depth--;
                        if (depth == 0) {
                            end = i;
                            break;
                        }
                    }
                }
            }
            if (end != std::string::npos) {
                return s.substr(start, end - start + 1);
            }
        }
        trim(s);
        return s;
    }
};

class GroqClient : public LLMClientBase {
public:
    explicit GroqClient(const std::string &key) : LLMClientBase(key) {}
    ChatResult fetch(const std::string &model,
                     const std::vector<std::pair<std::string, std::string>> &messages,
                     double temperature,
                     int max_tokens,
                     const json &schema = json(), int timeout_sec = 12) override {
        const bool wants_structured = !schema.is_null() && !schema.empty();
        const bool model_supports_json_mode = model_supports_forced_json(model);
        const bool schema_is_simple = is_simple_schema(schema);
        const bool use_forced_json =
            wants_structured && model_supports_json_mode && schema_is_simple;
        json msgs = json::array();
        bool has_system = false;
        for (const auto &[role, content] : messages) {
            if (role == "system") has_system = true;
            msgs.push_back({{"role", role}, {"content", content}});
        }
        if (wants_structured && !use_forced_json) {
            const std::string json_hint =
                "You must reply with a single valid JSON object and nothing else. "
                "No markdown, no code fences, no commentary. "
                "The JSON must be parseable by a strict parser.";
            if (has_system) {
                for (auto &m : msgs) {
                    if (m["role"] == "system") {
                        m["content"] = json_hint + "\n\n" + m["content"].get<std::string>();
                        break;
                    }
                }
            } else {
                msgs.insert(msgs.begin(), {{"role", "system"}, {"content", json_hint}});
            }
        }
        json payload = {
            {"model", model},
            {"temperature", temperature},
            {"max_tokens", max_tokens},
            {"messages", msgs}
        };
        if (use_forced_json) {
            payload["response_format"] = {{"type", "json_object"}};
        }
        std::string resp, err_out;
        long http_code = 0;
        if (!http_post_json("https://api.groq.com/openai/v1/chat/completions",
                            {"Authorization: Bearer " + api_key,
                             "Content-Type: application/json"},
                            payload, timeout_sec, resp, &err_out, &http_code)) {
            ErrorCategory cat = classify_error(http_code, err_out);
            if (http_code == 400 &&
                (err_out.find("json_validate_failed") != std::string::npos ||
                 err_out.find("Failed to validate JSON") != std::string::npos)) {
                cat = ErrorCategory::TRANSIENT;
            }
            return {"", false, err_out, cat};
        }
        try {
            json data = json::parse(resp);
            if (data.contains("error") && data["error"].is_object() &&
                data["error"].contains("message")) {
                std::string msg = data["error"]["message"].get<std::string>();
                ErrorCategory cat = classify_error(http_code, msg);
                if (msg.find("json_validate_failed") != std::string::npos ||
                    msg.find("Failed to validate JSON") != std::string::npos) {
                    cat = ErrorCategory::TRANSIENT;
                }
                return {"", false, msg, cat};
            }
            if (data.contains("choices") && data["choices"].is_array() &&
                !data["choices"].empty()) {
                auto &choice = data["choices"][0];
                if (choice.contains("message") &&
                    choice["message"].contains("content") &&
                    choice["message"]["content"].is_string()) {
                    std::string content =
                        choice["message"]["content"].get<std::string>();
                    if (wants_structured) {
                        content = extract_json_payload(content);
                    }
                    return {content, true, "", ErrorCategory::NONE};
                }
            }
            return {"", false, "Unexpected response structure",
                    ErrorCategory::TRANSIENT};
        } catch (const std::exception &e) {
            return {"", false, std::string("JSON parse error: ") + e.what(),
                    ErrorCategory::TRANSIENT};
        }
    }
private:
    static bool model_supports_forced_json(const std::string &model) {
        static const std::vector<std::string> reliable = {
            "llama-3.3-70b-versatile", "llama-3.1-70b-versatile",
            "llama-3.1-8b-instant", "llama3-70b-8192",
            "llama3-8b-8192", "gemma2-9b-it", "mixtral-8x7b-32768"
        };
        for (const auto &m : reliable) {
            if (model.find(m) != std::string::npos) return true;
        }
        return false;
    }
    static bool is_simple_schema(const json &schema) {
        if (schema.is_null() || schema.empty()) return true;
        std::string dumped = schema.dump();
        if (dumped.find("\"oneOf\"") != std::string::npos) return false;
        if (dumped.find("\"anyOf\"") != std::string::npos) return false;
        if (dumped.find("\"allOf\"") != std::string::npos) return false;
        if (dumped.size() > 2500) return false;
        return true;
    }
};

class OpenRouterClient : public LLMClientBase {
public:
    OpenRouterClient(const std::string &key, int rpm = 30)
        : LLMClientBase(key), limiter(rpm) {}
    ChatResult fetch(const std::string &model,
                     const std::vector<std::pair<std::string, std::string>> &messages,
                     double temperature,
                     int max_tokens,
                     const json &schema = json(), int timeout_sec = 12) override {
        limiter.acquire();
        const bool wants_structured = !schema.is_null() && !schema.empty();
        const bool use_forced_json =
            wants_structured && model_supports_forced_json(model);
        json msgs = json::array();
        bool has_system = false;
        for (const auto &[role, content] : messages) {
            if (role == "system") has_system = true;
            msgs.push_back({{"role", role}, {"content", content}});
        }
        if (wants_structured && !use_forced_json) {
            const std::string json_hint =
                "Reply with a single valid JSON object only. "
                "No markdown fences, no commentary, no extra text. "
                "The response must be directly parseable by json.loads / nlohmann::json.";
            if (has_system) {
                for (auto &m : msgs) {
                    if (m["role"] == "system") {
                        m["content"] = json_hint + "\n\n" + m["content"].get<std::string>();
                        break;
                    }
                }
            } else {
                msgs.insert(msgs.begin(), {{"role", "system"}, {"content", json_hint}});
            }
        }
        json payload = {
            {"model", model},
            {"temperature", temperature},
            {"max_tokens", max_tokens},
            {"messages", msgs}
        };
        if (use_forced_json) {
            payload["response_format"] = {{"type", "json_object"}};
        }
        payload["provider"] = {
            {"allow_fallbacks", true},
            {"require_parameters", true}
        };
        std::string resp, err_out;
        long http_code = 0;
        if (!http_post_json("https://openrouter.ai/api/v1/chat/completions",
                            {"Authorization: Bearer " + api_key,
                             "Content-Type: application/json",
                             "HTTP-Referer: https://localhost",
                             "X-Title: CodingAgent"},
                            payload, timeout_sec, resp, &err_out, &http_code)) {
            return make_error_result(http_code, err_out, resp);
        }
        try {
            json data = json::parse(resp);
            if (data.contains("error") && data["error"].is_object()) {
                std::string msg;
                if (data["error"].contains("message") && data["error"]["message"].is_string()) {
                    msg = data["error"]["message"].get<std::string>();
                } else {
                    msg = data["error"].dump();
                }
                return make_error_result(http_code, msg, resp);
            }
            if (data.contains("choices") && data["choices"].is_array() && !data["choices"].empty()) {
                auto &choice = data["choices"][0];
                if (choice.contains("message") &&
                    choice["message"].contains("content") &&
                    choice["message"]["content"].is_string()) {
                    std::string content = choice["message"]["content"].get<std::string>();
                    if (wants_structured) {
                        content = extract_json_payload(content);
                    }
                    return {content, true, "", ErrorCategory::NONE};
                }
            }
            if (wants_structured) {
                std::string fallback = extract_json_fallback(data);
                if (!fallback.empty()) {
                    return {fallback, true, "", ErrorCategory::NONE};
                }
            }
            return {"", false, "Unexpected response structure", ErrorCategory::TRANSIENT};
        } catch (const std::exception &e) {
            return {"", false, std::string("JSON parse error: ") + e.what(), ErrorCategory::TRANSIENT};
        }
    }
private:
    RateLimiter limiter;
    static bool model_supports_forced_json(const std::string &model) {
        static const std::vector<std::string> reliable = {
            "openai/", "anthropic/", "google/gemini", "meta-llama/llama-3",
            "meta-llama/llama-3.1", "meta-llama/llama-3.3", "mistralai/mistral-large",
            "mistralai/mixtral", "qwen/qwen-2.5", "deepseek/deepseek-chat"
        };
        for (const auto &prefix : reliable) {
            if (model.find(prefix) != std::string::npos) return true;
        }
        return false;
    }
    ChatResult make_error_result(long http_code, const std::string &err_out, const std::string &raw_body) const {
        std::string msg = err_out;
        ErrorCategory cat = classify_error(http_code, err_out);
        try {
            json body = json::parse(raw_body.empty() ? err_out : raw_body);
            if (body.contains("error") && body["error"].is_object()) {
                auto &e = body["error"];
                if (e.contains("message") && e["message"].is_string()) {
                    msg = e["message"].get<std::string>();
                }
                if (msg.find("free-models-per-day") != std::string::npos ||
                    msg.find("Rate limit exceeded") != std::string::npos) {
                    cat = ErrorCategory::QUOTA;
                }
            }
        } catch (...) {}
        if (msg.find("json_validate") != std::string::npos ||
            msg.find("Failed to validate JSON") != std::string::npos) {
            cat = ErrorCategory::TRANSIENT;
        }
        return {"", false, msg, cat};
    }
    static std::string extract_json_fallback(const json &data) {
        if (data.is_object() && data.contains("content") && data["content"].is_string()) {
            return extract_json_payload(data["content"].get<std::string>());
        }
        if (data.is_object() && data.contains("tool")) {
            return data.dump();
        }
        return "";
    }
};

class GoogleClient : public LLMClientBase {
public:
    explicit GoogleClient(const std::string &key) : LLMClientBase(key) {}
    ChatResult fetch(const std::string &model,
                     const std::vector<std::pair<std::string, std::string>> &messages,
                     double temperature,
                     int max_tokens,
                     const json &schema = json(), int timeout_sec = 12) override {
        std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" + model + ":generateContent?key=" + api_key;
        const bool wants_structured = !schema.is_null() && !schema.empty();
        const bool use_full_schema = wants_structured && is_schema_safe_for_google(schema);
        json contents = json::array();
        json sys_parts = json::array();
        for (const auto &[role, text] : messages) {
            if (role == "system") {
                sys_parts.push_back({{"text", text}});
            } else {
                std::string mapped = (role == "user") ? "user" : "model";
                if (!contents.empty() && contents.back()["role"] == mapped) {
                    contents.back()["parts"][0]["text"] =
                        contents.back()["parts"][0]["text"].get<std::string>() + "\n\n" + text;
                } else {
                    json item = json::object();
                    item["role"] = mapped;
                    item["parts"] = json::array({json{{"text", text}}});
                    contents.push_back(item);
                }
            }
        }
        if (contents.empty()) {
            contents.push_back({{"role", "user"}, {"parts", json::array({json{{"text", "Begin task."}}})}});
        }
        if (wants_structured && !use_full_schema) {
            sys_parts.push_back({
                {"text", "You must reply with a single valid JSON object only. "
                         "No markdown fences, no commentary, no extra text. "
                         "The response must be directly parseable by a strict JSON parser."}
            });
        }
        json genConfig = {
            {"temperature", temperature},
            {"maxOutputTokens", max_tokens}
        };
        if (wants_structured) {
            genConfig["responseMimeType"] = "application/json";
            if (use_full_schema) {
                genConfig["responseSchema"] = to_google_response_schema(schema);
            }
        }
        json payload = {
            {"contents", contents},
            {"generationConfig", genConfig}
        };
        if (!sys_parts.empty()) {
            payload["systemInstruction"] = {{"parts", sys_parts}};
        }
        std::string resp, err_out;
        long http_code = 0;
        if (!http_post_json(url, {"Content-Type: application/json"}, payload, timeout_sec, resp, &err_out, &http_code)) {
            return make_error_result(http_code, err_out, resp);
        }
        try {
            json data = json::parse(resp);
            if (data.contains("error") && data["error"].is_object() && data["error"].contains("message")) {
                std::string msg = data["error"]["message"].get<std::string>();
                return make_error_result(http_code, msg, resp);
            }
            if (data.contains("promptFeedback") && data["promptFeedback"].contains("blockReason")) {
                std::string reason = data["promptFeedback"]["blockReason"].get<std::string>();
                return {"", false, "Prompt blocked by safety filter: " + reason, ErrorCategory::PERMANENT};
            }
            if (!data.contains("candidates") || !data["candidates"].is_array() || data["candidates"].empty()) {
                return {"", false, "No candidates returned", ErrorCategory::TRANSIENT};
            }
            auto &cand = data["candidates"][0];
            if (cand.contains("finishReason") && cand["finishReason"].is_string()) {
                std::string fr = cand["finishReason"].get<std::string>();
                if (fr == "SAFETY" || fr == "RECITATION" || fr == "OTHER") {
                    return {"", false, "Generation stopped: " + fr,
                            fr == "SAFETY" ? ErrorCategory::PERMANENT : ErrorCategory::TRANSIENT};
                }
            }
            if (cand.contains("content") && cand["content"].contains("parts") &&
                cand["content"]["parts"].is_array() && !cand["content"]["parts"].empty()) {
                std::string full_text;
                for (auto &part : cand["content"]["parts"]) {
                    if (part.contains("text") && part["text"].is_string()) {
                        full_text += part["text"].get<std::string>();
                    }
                }
                if (!full_text.empty()) {
                    if (wants_structured) {
                        full_text = extract_json_payload(full_text);
                    }
                    return {full_text, true, "", ErrorCategory::NONE};
                }
            }
            return {"", false, "Empty candidate content", ErrorCategory::TRANSIENT};
        } catch (const std::exception &e) {
            return {"", false, std::string("JSON parse error: ") + e.what(), ErrorCategory::TRANSIENT};
        }
    }
private:
    ChatResult make_error_result(long http_code, const std::string &err_out, const std::string &raw_body) const {
        return {"", false, err_out, classify_error(http_code, err_out)};
    }
    static bool is_schema_safe_for_google(const json &schema) {
        if (schema.is_null() || schema.empty()) return false;
        std::string dumped = schema.dump();
        if (dumped.find("\"anyOf\"") != std::string::npos ||
            dumped.find("\"oneOf\"") != std::string::npos ||
            dumped.find("\"allOf\"") != std::string::npos) return false;
        return true;
    }
};

class LLMBackendManager {
public:
    struct Config {
        std::map<std::string, std::string> models;
        std::vector<std::string> priority;
        int global_rpm = 30;
    };
    std::unordered_set<std::string> dead_models_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> cooldown_;
    std::mutex health_mu_;
    LLMBackendManager(const Config &cfg) : config(cfg), limiter(cfg.global_rpm) {
        uint64_t seed = std::chrono::steady_clock::now().time_since_epoch().count();
        rng_.seed(seed);
        const char *groq = getenv("GROQ_API_KEY");
        const char *openrouter = getenv("OPENROUTER_API_KEY");
        const char *google = getenv("GOOGLE_API_KEY");
        if (groq) clients["groq"] = std::make_shared<GroqClient>(groq);
        if (openrouter) clients["openrouter"] = std::make_shared<OpenRouterClient>(openrouter);
        if (google) clients["google"] = std::make_shared<GoogleClient>(google);
        fallback_models_["groq"] = {
            "openai/gpt-oss-20b",
            "openai/gpt-oss-120b",
            "qwen/qwen3.6-27b",
            "llama-3.1-8b-instant"
        };
        fallback_models_["openrouter"] = {
            "openrouter/free",
            "openai/gpt-oss-20b:free",
            "openai/gpt-oss-120b:free",
            "meta-llama/llama-3.2-3b-instruct:free",
            "cohere/north-mini-code:free"
        };
        fallback_models_["google"] = {
            "gemini-2.5-flash-lite",
            "gemini-2.5-flash",
            "gemini-3.6-flash"
        };
        for (auto &b : config.priority) {
            if (clients.count(b)) {
                current_backend = b;
                break;
            }
        }
        if (current_backend.empty()) {
            for (auto &[b, _] : clients) {
                current_backend = b;
                break;
            }
        }
        if (current_backend.empty())
            throw std::runtime_error("No LLM backend available");
        for (const auto &[b_name, _] : clients) {
            std::vector<std::string> m_list;
            if (config.models.count(b_name)) {
                m_list.push_back(config.models.at(b_name));
            }
            if (fallback_models_.count(b_name)) {
                for (const auto &m : fallback_models_[b_name]) {
                    if (std::find(m_list.begin(), m_list.end(), m) == m_list.end()) {
                        m_list.push_back(m);
                    }
                }
            }
            active_models_[b_name] = m_list;
            auto &st = backend_stats[b_name];
            st.alpha = 2.0;
            st.beta = 1.0;
            st.ewma_latency = 1.0;
            for (size_t i = 0; i < config.priority.size(); ++i) {
                if (config.priority[i] == b_name) {
                    st.alpha += static_cast<double>(config.priority.size() - i) * 2.0;
                    break;
                }
            }
        }
    }
    void abort() { aborted = true; }
    void reset_abort() { aborted = false; }
    void reset_cooldowns() {
        std::lock_guard<std::mutex> lk(state_mtx);
        cooldown_until.clear();
    }
    static double extract_retry_delay_seconds(const std::string &err) {
        std::string low = err;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        static const std::regex re_retry(
            R"((?:retry|try again|please retry|wait)\s+(?:in\s+)?(\d+(?:.\d+)?)\s?(ms|s|sec|secs|second|seconds|m|min|mins|minute|minutes|h|hr|hrs|hour|hours)?)",
            std::regex::icase);
        static const std::regex re_reset(R"(reset\s=\s(\d+))");
        double best = 0.0;
        std::smatch m;
        if (std::regex_search(low, m, re_retry) && m.size() >= 2) {
            double val = std::stod(m[1].str());
            std::string unit = m.size() > 2 ? m[2].str() : "";
            std::transform(unit.begin(), unit.end(), unit.begin(), ::tolower);
            if (unit == "ms") {
                best = std::max(best, val / 1000.0);
            } else if (unit.empty() || unit[0] == 's') {
                best = std::max(best, val);
            } else if (unit[0] == 'm') {
                best = std::max(best, val * 60.0);
            } else if (unit[0] == 'h') {
                best = std::max(best, val * 3600.0);
            }
        }
        if (std::regex_search(err, m, re_reset) && m.size() >= 2) {
            try {
                double epoch_ms = std::stod(m[1].str());
                double now_ms = get_time() * 1000.0;
                if (epoch_ms > now_ms)
                    best = std::max(best, (epoch_ms - now_ms) / 1000.0);
            } catch (...) {}
        }
        return best;
    }
    void set_cooldown(const std::string &backend, ErrorCategory category,
                      const std::string &error_msg = "") {
        std::lock_guard<std::mutex> lk(state_mtx);
        double now = get_time();
        if (category == ErrorCategory::PERMANENT) {
            cooldown_until[backend] = now + 120.0;
        } else {
            cooldown_until[backend] = now + 3.0;
        }
    }
    ChatResult chat(const std::vector<std::pair<std::string, std::string>> &messages,
                    double temperature = 0.0,
                    int max_tokens = 2048,
                    const json &schema = json()) {
        if (aborted) return {"", false, "Aborted by agent.", ErrorCategory::PERMANENT};
        const double deadline = get_time() + kChatDeadlineSec;
        limiter.acquire();
        if (aborted) return {"", false, "Aborted by agent.", ErrorCategory::PERMANENT};
        const int max_retry_passes = 2;
        for (int pass = 0; pass <= max_retry_passes; ++pass) {
            if (aborted) return {"", false, "Aborted by agent.", ErrorCategory::PERMANENT};
            if (get_time() > deadline) return {"", false, "Deadline exceeded", ErrorCategory::TRANSIENT};
            std::vector<std::string> order = get_backend_order();
            for (const auto &backend : order) {
                if (aborted) return {"", false, "Aborted by agent.", ErrorCategory::PERMANENT};
                if (get_time() > deadline) return {"", false, "Deadline exceeded", ErrorCategory::TRANSIENT};
                if (is_on_cooldown(backend)) continue;
                auto it = clients.find(backend);
                if (it == clients.end()) continue;
                std::vector<std::string> model_list = get_active_models_sorted(backend);
                bool any_usable = false;
                for (const auto &m : model_list) {
                    if (model_usable_now(backend, m)) {
                        any_usable = true;
                        break;
                    }
                }
                if (!any_usable) continue;
                bool backend_failed = false;
                int transient_failures = 0;
                ErrorCategory worst_category = ErrorCategory::NONE;
                std::string worst_error;
                int models_remaining = static_cast<int>(model_list.size());
                for (const auto &model : model_list) {
                    if (aborted) return {"", false, "Aborted by agent.", ErrorCategory::PERMANENT};
                    if (get_time() > deadline) return {"", false, "Deadline exceeded", ErrorCategory::TRANSIENT};
                    if (!model_usable_now(backend, model)) {
                        models_remaining--;
                        continue;
                    }
                    double remaining = deadline - get_time();
                    if (remaining < 1.5) break;
                    double ewma = get_model_ewma_latency(backend, model);
                    double fair_share = remaining / std::max(1, models_remaining);
                    double budget = std::max(2.0, std::min({ewma * 2.5, fair_share, kMaxAttemptSec}));
                    if (budget < 2.0) {
                        models_remaining--;
                        continue;
                    }
                    int timeout_int = static_cast<int>(std::max(1.0, budget));
                    double t_start = get_time();
                    ChatResult r = it->second->fetch(model, messages, temperature, max_tokens, schema, timeout_int);
                    double elapsed = get_time() - t_start;
                    models_remaining--;
                    if (r.ok) {
                        record_model_success(backend, model, elapsed);
                        clear_backend_cooldown(backend);
                        record_backend_success(backend, elapsed);
                        {
                            std::lock_guard<std::mutex> lk(state_mtx);
                            current_backend = backend;
                        }
                        return r;
                    }
                    record_model_failure(backend, model, r.category, r.error);
                    std::cerr << "[LLM] backend=" << backend << " model=" << model
                              << " error=" << r.error << " category=" << static_cast<int>(r.category)
                              << " elapsed=" << elapsed << "s\n";
                    if (is_model_not_found_error(r.category, r.error)) {
                        prune_model(backend, model);
                        continue;
                    }
                    if (r.category == ErrorCategory::PERMANENT || r.category == ErrorCategory::QUOTA) {
                        record_backend_failure(backend);
                        if (worst_category == ErrorCategory::NONE ||
                            (r.category == ErrorCategory::PERMANENT && worst_category != ErrorCategory::PERMANENT)) {
                            worst_category = r.category;
                            worst_error = r.error;
                        } else if (r.category == worst_category) {
                            worst_error = r.error;
                        }
                        continue;
                    }
                    transient_failures++;
                    if (worst_category == ErrorCategory::NONE) {
                        worst_category = ErrorCategory::TRANSIENT;
                        worst_error = r.error;
                    }
                    if (transient_failures >= 2) {
                        record_backend_failure(backend);
                        set_cooldown(backend, ErrorCategory::TRANSIENT, "Too many transient failures");
                        backend_failed = true;
                        break;
                    }
                }
                if (!backend_failed) {
                    if (worst_category == ErrorCategory::PERMANENT) {
                        set_cooldown(backend, ErrorCategory::PERMANENT, worst_error);
                    } else if (transient_failures > 0) {
                        set_cooldown(backend, ErrorCategory::TRANSIENT, worst_error);
                    }
                }
            }
            if (pass == max_retry_passes) break;
            double wait_time = min_cooldown_remaining();
            double remaining = deadline - get_time();
            if (wait_time <= 0.0 || wait_time == std::numeric_limits<double>::infinity() ||
                wait_time > 30.0 || wait_time > remaining) {
                break;
            }
            double sleep_increment = 0.1;
            double slept = 0.0;
            while (slept < wait_time && !aborted) {
                double step = std::min(sleep_increment, wait_time - slept);
                std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(step * 1000)));
                slept += step;
            }
        }
        return {"", false, "All backends exhausted.", ErrorCategory::PERMANENT};
    }
    json structured_chat(const std::vector<std::pair<std::string, std::string>> &messages,
                         const json &schema,
                         double temperature = 0.0,
                         int max_tokens = 512) {
        std::string system =
            "You are a precise JSON-only assistant. Output ONLY valid JSON matching the schema below.\n"
            "Schema:\n" + schema.dump(2) + "\nOutput the raw JSON immediately without markdown backticks.";
        std::vector<std::pair<std::string, std::string>> full = {{"system", system}};
        full.insert(full.end(), messages.begin(), messages.end());
        ChatResult r = chat(full, temperature, max_tokens, schema);
        if (!r.ok) return json::object();
        std::string content = r.content;
        size_t pos = 0;
        while ((pos = content.find("```")) != std::string::npos) {
            size_t end = content.find("\n", pos + 3);
            if (end == std::string::npos) end = content.size();
            content.erase(pos, end - pos + 1);
        }
        std::string extracted = extract_json_with_bracket_matching(content);
        content = extracted.empty() ? content : extracted;
        try {
            return json::parse(content);
        } catch (...) {
            std::string recovered = extract_json_with_bracket_matching(
                smart_sanitize_for_structured(r.content));
            if (!recovered.empty()) {
                try {
                    return json::parse(recovered);
                } catch (...) {}
            }
        }
        return json::object();
    }
    void clear_backend_cooldown(const std::string &backend) {
        std::lock_guard<std::mutex> lk(state_mtx);
        cooldown_until.erase(backend);
    }
    double min_cooldown_remaining() {
        std::lock_guard<std::mutex> lk(state_mtx);
        double now = get_time();
        double min_wait = std::numeric_limits<double>::infinity();
        for (auto &[_, until] : cooldown_until) {
            if (until > now && until != std::numeric_limits<double>::infinity()) {
                min_wait = std::min(min_wait, until - now);
            }
        }
        return min_wait == std::numeric_limits<double>::infinity() ? 0.0 : min_wait;
    }
    bool model_is_usable(const std::string &key) {
        std::lock_guard<std::mutex> lk(health_mu_);
        if (dead_models_.count(key)) return false;
        auto it = cooldown_.find(key);
        if (it == cooldown_.end()) return true;
        if (std::chrono::steady_clock::now() < it->second) return false;
        cooldown_.erase(it);
        return true;
    }
    void report_model_failure(const std::string &key,
                              int category,
                              const std::string &message) {
        std::lock_guard<std::mutex> lk(health_mu_);
        if (category == 3) { dead_models_.insert(key); return; }
        double wait_s = 20.0;
        std::smatch m;
        if (std::regex_search(message, m,
                              std::regex(R"(try again in (?:(\d+)m)?([\d.]+)s)"))) {
            double mins = m[1].matched ? std::stod(m[1].str()) : 0.0;
            wait_s = mins * 60.0 + std::stod(m[2].str());
        }
        cooldown_[key] = std::chrono::steady_clock::now() +
                         std::chrono::milliseconds(static_cast<long>(wait_s * 1000));
    }
private:
    static constexpr double kChatDeadlineSec = 30.0;
    static constexpr double kMaxAttemptSec = 20.0;
    struct BackendStats {
        double alpha = 1.0;
        double beta = 1.0;
        double ewma_latency = 1.0;
        bool initialized = false;
        double last_success_time = 0.0;
        int total_calls = 0;
    };
    struct ModelHealth {
        int consecutive_failures = 0;
        double cooldown_until = 0.0;
        bool blacklisted = false;
        double ewma_latency = 2.0;
    };
    Config config;
    std::map<std::string, std::shared_ptr<LLMClientBase>> clients;
    std::string current_backend;
    std::map<std::string, double> cooldown_until;
    std::unordered_map<std::string, BackendStats> backend_stats;
    std::unordered_map<std::string, std::vector<std::string>> active_models_;
    mutable std::mutex state_mtx;
    RateLimiter limiter;
    std::atomic<bool> aborted{false};
    std::mt19937 rng_;
    std::unordered_map<std::string, std::vector<std::string>> fallback_models_;
    std::unordered_map<std::string, ModelHealth> model_health_;
    std::mutex health_mtx_;
    static std::string model_key(const std::string &backend, const std::string &model) {
        return backend + "\n" + model;
    }
    static bool is_permanently_dead_model(ErrorCategory category, const std::string &err) {
        if (category != ErrorCategory::PERMANENT) return false;
        std::string low = err;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        return low.find("model_not_found") != std::string::npos ||
               low.find("does not exist") != std::string::npos ||
               low.find("not found") != std::string::npos ||
               low.find("unavailable") != std::string::npos ||
               low.find("invalid model") != std::string::npos ||
               low.find("decommission") != std::string::npos ||
               low.find("unsupported model") != std::string::npos;
    }
    bool model_usable_now(const std::string &backend, const std::string &model) {
        std::lock_guard<std::mutex> lk(health_mtx_);
        auto it = model_health_.find(model_key(backend, model));
        if (it == model_health_.end()) return true;
        const auto &h = it->second;
        if (h.blacklisted) return false;
        return get_time() >= h.cooldown_until;
    }
    double get_model_ewma_latency(const std::string &backend, const std::string &model) {
        std::lock_guard<std::mutex> lk(health_mtx_);
        auto it = model_health_.find(model_key(backend, model));
        if (it == model_health_.end()) return 2.0;
        return it->second.ewma_latency;
    }
    void record_model_success(const std::string &backend, const std::string &model,
                              double elapsed_sec) {
        std::lock_guard<std::mutex> lk(health_mtx_);
        auto &h = model_health_[model_key(backend, model)];
        h.consecutive_failures = 0;
        h.cooldown_until = 0.0;
        h.ewma_latency = 0.7 * h.ewma_latency + 0.3 * std::max(0.1, elapsed_sec);
    }
    void record_model_failure(const std::string &backend, const std::string &model,
                              ErrorCategory category, const std::string &err) {
        std::lock_guard<std::mutex> lk(health_mtx_);
        const std::string key = model_key(backend, model);
        auto &h = model_health_[key];
        h.consecutive_failures++;
        h.ewma_latency = std::min(h.ewma_latency * 1.3, 30.0);
        if (is_permanently_dead_model(category, err)) {
            h.blacklisted = true;
            prune_model_unlocked(backend, model);
            return;
        }
        if (category == ErrorCategory::PERMANENT) {
            h.cooldown_until = get_time() + 120.0;
            return;
        }
        double delay = 8.0 * static_cast<double>(1 << std::min(h.consecutive_failures - 1, 4));
        if (category == ErrorCategory::QUOTA) {
            double suggested = extract_retry_delay_seconds(err);
            if (suggested > 0.0) delay = std::max(delay, std::min(suggested, 120.0));
        }
        h.cooldown_until = get_time() + std::min(delay, 120.0);
    }
    void record_backend_success(const std::string &backend, double elapsed_sec = 0.0) {
        std::lock_guard<std::mutex> lk(state_mtx);
        auto &st = backend_stats[backend];
        st.alpha = st.alpha * 0.95 + 1.0;
        st.total_calls++;
        if (elapsed_sec > 0.0) {
            st.ewma_latency = 0.7 * st.ewma_latency + 0.3 * elapsed_sec;
        }
        st.last_success_time = get_time();
    }
    void record_backend_failure(const std::string &backend) {
        std::lock_guard<std::mutex> lk(state_mtx);
        auto &st = backend_stats[backend];
        st.beta = st.beta * 0.95 + 1.0;
        st.total_calls++;
        st.ewma_latency = std::max(st.ewma_latency * 1.5, 5.0);
    }
    bool is_on_cooldown(const std::string &backend) {
        std::lock_guard<std::mutex> lk(state_mtx);
        auto it = cooldown_until.find(backend);
        if (it == cooldown_until.end()) return false;
        if (get_time() >= it->second) {
            cooldown_until.erase(it);
            return false;
        }
        return true;
    }
    std::vector<std::string> get_backend_order() {
        std::lock_guard<std::mutex> lk(state_mtx);
        std::vector<std::pair<double, std::string>> scored;
        double now = get_time();
        for (auto &[name, client] : clients) {
            auto it = cooldown_until.find(name);
            if (it != cooldown_until.end() && now < it->second) continue;
            auto &st = backend_stats[name];
            double success_rate = st.alpha / (st.alpha + st.beta + 1e-9);
            double exploration_bonus = 0.0;
            if (st.total_calls > 0) {
                exploration_bonus = std::sqrt(2.0 * std::log(st.total_calls + 1) / st.total_calls);
            } else {
                exploration_bonus = 1.0;
            }
            double ucb_score = success_rate + exploration_bonus;
            double latency_factor = std::max(0.05, st.ewma_latency);
            double qos_score = ucb_score / latency_factor;
            scored.push_back({qos_score, name});
        }
        std::sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) {
            return a.first > b.first;
        });
        std::vector<std::string> order;
        for (auto &[score, name] : scored) order.push_back(name);
        return order;
    }
    std::vector<std::string> get_active_models(const std::string &backend) {
        std::lock_guard<std::mutex> lk(state_mtx);
        auto it = active_models_.find(backend);
        if (it != active_models_.end()) {
            return it->second;
        }
        return {};
    }
    std::vector<std::string> get_active_models_sorted(const std::string &backend) {
        std::vector<std::string> models = get_active_models(backend);
        std::vector<std::pair<double, std::string>> scored;
        scored.reserve(models.size());
        for (const auto &m : models) {
            double ewma = get_model_ewma_latency(backend, m);
            scored.push_back({ewma, m});
        }
        std::sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });
        std::vector<std::string> result;
        result.reserve(scored.size());
        for (auto &[lat, name] : scored) result.push_back(name);
        return result;
    }
    void prune_model(const std::string &backend, const std::string &model) {
        std::lock_guard<std::mutex> lk(state_mtx);
        auto it = active_models_.find(backend);
        if (it != active_models_.end()) {
            auto &vec = it->second;
            vec.erase(std::remove(vec.begin(), vec.end(), model), vec.end());
        }
    }
    void prune_model_unlocked(const std::string &backend, const std::string &model) {
        auto it = active_models_.find(backend);
        if (it != active_models_.end()) {
            auto &vec = it->second;
            vec.erase(std::remove(vec.begin(), vec.end(), model), vec.end());
        }
    }
    static bool is_model_not_found_error(ErrorCategory category, const std::string &error) {
        if (category != ErrorCategory::PERMANENT) return false;
        std::string low = error;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        return (low.find("model_not_found") != std::string::npos ||
                low.find("does not exist") != std::string::npos ||
                low.find("404") != std::string::npos ||
                low.find("not found") != std::string::npos ||
                low.find("unavailable model") != std::string::npos);
    }
};
