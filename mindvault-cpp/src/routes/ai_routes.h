#pragma once
// AI routes: /api/ai/*

#include "../database.h"
#include "../services/search_service.h"
#include "../utils/response.h"
#include "crow_all.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace mindvault::routes {

#ifdef _WIN32
inline std::string HttpPost(const std::wstring& host, int port, const std::wstring& path, const std::string& body, const std::string& auth = "") {
    HINTERNET hSession = WinHttpOpen(L"MindVault/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!hSession) return "";

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

    DWORD flags = (port == 443) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    std::wstring headers = L"Content-Type: application/json";
    if (!auth.empty()) {
        headers += L"\r\nAuthorization: Bearer ";
        headers += std::wstring(auth.begin(), auth.end());
    }

    BOOL result = WinHttpSendRequest(hRequest, headers.c_str(), -1,
        (LPVOID)body.c_str(), (DWORD)body.size(), (DWORD)body.size(), 0);

    if (!result) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    WinHttpReceiveResponse(hRequest, nullptr);

    std::string response;
    DWORD bytesAvailable = 0;
    do {
        WinHttpQueryDataAvailable(hRequest, &bytesAvailable);
        if (bytesAvailable > 0) {
            std::vector<char> buffer(bytesAvailable);
            DWORD bytesRead = 0;
            WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead);
            response.append(buffer.data(), bytesRead);
        }
    } while (bytesAvailable > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return response;
}

inline std::string CallOnlineAPI(const std::string& api_url, const std::string& api_key, const std::string& model, const std::string& prompt) {
    // Parse URL: https://host:port/path
    std::string url = api_url;
    int port = 443;
    bool is_https = true;

    if (url.substr(0, 7) == "http://") {
        url = url.substr(7);
        is_https = false;
        port = 80;
    } else if (url.substr(0, 8) == "https://") {
        url = url.substr(8);
    }

    auto slash_pos = url.find('/');
    std::string host_str = (slash_pos != std::string::npos) ? url.substr(0, slash_pos) : url;
    std::wstring path = (slash_pos != std::string::npos)
        ? std::wstring(url.begin() + slash_pos, url.end())
        : L"/v1/chat/completions";

    auto colon_pos = host_str.find(':');
    if (colon_pos != std::string::npos) {
        port = std::stoi(host_str.substr(colon_pos + 1));
        host_str = host_str.substr(0, colon_pos);
    } else if (is_https) {
        port = 443;
    }

    std::wstring host(host_str.begin(), host_str.end());

    // Build OpenAI-compatible request
    nlohmann::json req = {
        {"model", model},
        {"messages", nlohmann::json::array({
            {{"role", "system"}, {"content", "你是一个知识库助手。请基于参考资料回答问题。如果资料无法回答，请明确说明。"}},
            {{"role", "user"}, {"content", prompt}}
        })},
        {"temperature", 0.7},
        {"max_tokens", 2000}
    };

    return HttpPost(host, port, path, req.dump(), api_key);
}
#endif

inline void RegisterAIRoutes(crow::App<>& app, Database& db) {
    auto search_svc = std::make_shared<services::SearchService>(db);

    // GET /api/ai/status - check AI providers
    CROW_ROUTE(app, "/api/ai/status").methods("GET"_method)
    ([]() {
        nlohmann::json status = {
            {"ollama", false},
            {"providers", nlohmann::json::array({
                nlohmann::json{{"id", "tongyi"}, {"name", "通义千问"}, {"url", "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"}, {"models", nlohmann::json::array({"qwen-turbo", "qwen-plus", "qwen-max"})}},
                nlohmann::json{{"id", "doubao"}, {"name", "豆包"}, {"url", "https://ark.cn-beijing.volces.com/api/v3/chat/completions"}, {"models", nlohmann::json::array({"doubao-pro-32k", "doubao-lite-32k"})}},
                nlohmann::json{{"id", "openai"}, {"name", "OpenAI"}, {"url", "https://api.openai.com/v1/chat/completions"}, {"models", nlohmann::json::array({"gpt-4o-mini", "gpt-4o", "gpt-3.5-turbo"})}},
                nlohmann::json{{"id", "custom"}, {"name", "自定义"}, {"url", ""}, {"models", nlohmann::json::array({})}}
            })}
        };

#ifdef _WIN32
        try {
            std::string res = HttpPost(L"localhost", 11434, L"/api/tags", "");
            if (!res.empty()) {
                auto json = nlohmann::json::parse(res);
                auto models = json.value("models", nlohmann::json::array());
                std::vector<std::string> model_list;
                for (auto& m : models) model_list.push_back(m.value("name", ""));
                status["ollama"] = true;
                status["ollama_models"] = model_list;
            }
        } catch (...) {}
#endif

        return crow::response(utils::Success(status).dump());
    });

    // POST /api/ai/chat - AI chat with RAG
    CROW_ROUTE(app, "/api/ai/chat").methods("POST"_method)
    ([search_svc](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string question = body.value("question", std::string(""));
            std::string provider = body.value("provider", std::string("ollama"));
            std::string model = body.value("model", std::string("qwen-turbo"));
            std::string api_key = body.value("api_key", std::string(""));
            std::string api_url = body.value("api_url", std::string(""));

            if (question.empty()) {
                return crow::response(400, utils::Error("Question is required").dump());
            }

            // RAG: Search relevant notes
            auto results = search_svc->Search(question, 5);

            // Build context
            std::string context;
            for (size_t i = 0; i < results.size(); ++i) {
                auto& r = results[i];
                std::string title = r.value("title", std::string(""));
                std::string snippet = r.value("content_highlight", std::string(""));
                if (snippet.empty()) snippet = r.value("title_highlight", std::string(""));
                size_t pos;
                while ((pos = snippet.find(">>>")) != std::string::npos) snippet.replace(pos, 3, "");
                while ((pos = snippet.find("<<<")) != std::string::npos) snippet.replace(pos, 3, "");
                context += "[来源" + std::to_string(i + 1) + ": " + title + "]\n" + snippet + "\n\n";
            }

            std::string prompt = "## 参考资料：\n" + context + "\n## 问题：\n" + question;
            std::string answer;

#ifdef _WIN32
            if (provider == "ollama") {
                nlohmann::json ollama_req = {{"model", model}, {"prompt", "你是一个知识库助手。\n\n" + prompt}, {"stream", false}};
                std::string res = HttpPost(L"localhost", 11434, L"/api/generate", ollama_req.dump());
                if (!res.empty()) {
                    auto r = nlohmann::json::parse(res);
                    answer = r.value("response", "");
                }
            } else {
                // Default URLs
                if (api_url.empty()) {
                    if (provider == "tongyi") api_url = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
                    else if (provider == "doubao") api_url = "https://ark.cn-beijing.volces.com/api/v3/chat/completions";
                    else if (provider == "openai") api_url = "https://api.openai.com/v1/chat/completions";
                }
                if (!api_url.empty() && !api_key.empty()) {
                    std::string res = CallOnlineAPI(api_url, api_key, model, prompt);
                    if (!res.empty()) {
                        auto r = nlohmann::json::parse(res);
                        if (r.contains("choices") && r["choices"].size() > 0) {
                            answer = r["choices"][0]["message"]["content"];
                        } else if (r.contains("error")) {
                            answer = "API 错误: " + r["error"].value("message", "Unknown");
                        }
                    }
                }
            }
#endif

            if (answer.empty()) {
                answer = "无法获取 AI 回答。请检查：\n1. Ollama 是否运行\n2. API Key 是否正确\n\n以下是 RAG 检索结果：\n\n" + context;
            }

            nlohmann::json response = {{"answer", answer}, {"sources", results}, {"provider", provider}, {"model", model}};
            return crow::response(utils::Success(response).dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });
}

} // namespace mindvault::routes
