#pragma once
// AI routes: /api/ai/*

#include "../database.h"
#include "../services/search_service.h"
#include "../services/ai_action_service.h"
#include "../services/graph_qa_service.h"
#include "../services/function_calling_service.h"
#include "../utils/response.h"
#include "crow_all.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <sstream>
#include <mutex>
#include <unordered_map>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace mindvault::routes {

// ─── 截断常量 ───
constexpr int MAX_HISTORY_TURNS = 10;
constexpr int MAX_CONTEXT_TOKENS = 8000;
constexpr int SYSTEM_PROMPT_TOKENS = 200;

// ─── 并发控制：会话级互斥锁 ───
static std::mutex g_conv_mutex_map_mtx;
static std::unordered_map<int, std::unique_ptr<std::mutex>> g_conv_mutexes;

inline std::mutex& getConvMutex(int convId) {
    std::lock_guard<std::mutex> g(g_conv_mutex_map_mtx);
    auto& ptr = g_conv_mutexes[convId];
    if (!ptr) ptr = std::make_unique<std::mutex>();
    return *ptr;
}

// ─── Token 估算 ───
inline int estimateTokens(const std::string& text) {
    if (text.empty()) return 0;
    return static_cast<int>(text.length() / 3) + 1;
}

// ─── 从 Authorization header 提取 user_id ───
inline int64_t extractUserId(const crow::request& req) {
    std::string token = req.get_header_value("Authorization");
    if (token.empty() || token.size() < 8) return 0;
    if (token.substr(0, 7) != "Bearer ") return 0;
    token = token.substr(7);
    auto pos = token.find('_');
    if (pos == std::string::npos) return 0;
    try { return std::stoll(token.substr(0, pos)); }
    catch (...) { return 0; }
}

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

// ─── 调用在线模型 API（会话模式用，传 messages 数组）───
inline std::string CallOnlineAPIWithMessages(const std::string& api_url, const std::string& api_key, const std::string& model, const nlohmann::json& messages) {
    std::string url = api_url;
    int port = 443;
    bool is_https = true;

    if (url.substr(0, 7) == "http://") { url = url.substr(7); is_https = false; port = 80; }
    else if (url.substr(0, 8) == "https://") { url = url.substr(8); }

    auto slash_pos = url.find('/');
    std::string host_str = (slash_pos != std::string::npos) ? url.substr(0, slash_pos) : url;
    std::wstring path = (slash_pos != std::string::npos)
        ? std::wstring(url.begin() + slash_pos, url.end())
        : L"/v1/chat/completions";

    auto colon_pos = host_str.find(':');
    if (colon_pos != std::string::npos) {
        port = std::stoi(host_str.substr(colon_pos + 1));
        host_str = host_str.substr(0, colon_pos);
    } else if (is_https) { port = 443; }

    std::wstring host(host_str.begin(), host_str.end());

    nlohmann::json req = {
        {"model", model},
        {"messages", messages},
        {"temperature", 0.7},
        {"max_tokens", 2000}
    };

    return HttpPost(host, port, path, req.dump(), api_key);
}

// P2-2 function calling：带 tools 的 AI 调用（tools 为空时等价于普通调用）
inline std::string CallOnlineAPIWithTools(const std::string& api_url, const std::string& api_key,
                                           const std::string& model, const nlohmann::json& messages,
                                           const nlohmann::json& tools) {
    std::string url = api_url;
    int port = 443;
    bool is_https = true;

    if (url.substr(0, 7) == "http://") { url = url.substr(7); is_https = false; port = 80; }
    else if (url.substr(0, 8) == "https://") { url = url.substr(8); }

    auto slash_pos = url.find('/');
    std::string host_str = (slash_pos != std::string::npos) ? url.substr(0, slash_pos) : url;
    std::wstring path = (slash_pos != std::string::npos)
        ? std::wstring(url.begin() + slash_pos, url.end())
        : L"/v1/chat/completions";

    auto colon_pos = host_str.find(':');
    if (colon_pos != std::string::npos) {
        port = std::stoi(host_str.substr(colon_pos + 1));
        host_str = host_str.substr(0, colon_pos);
    } else if (is_https) { port = 443; }

    std::wstring host(host_str.begin(), host_str.end());

    nlohmann::json req = {
        {"model", model},
        {"messages", messages},
        {"temperature", 0.7},
        {"max_tokens", 2000}
    };
    // tools 非空时加入请求体
    if (!tools.is_null() && !tools.empty()) {
        req["tools"] = tools;
        req["tool_choice"] = "auto";
    }

    return HttpPost(host, port, path, req.dump(), api_key);
}

inline std::string CallOnlineAPI(const std::string& api_url, const std::string& api_key, const std::string& model, const std::string& prompt) {
    nlohmann::json messages = {
        {{"role", "system"}, {"content", "你是一个知识库助手。请基于参考资料回答问题。如果资料无法回答，请明确说明。"}},
        {{"role", "user"}, {"content", prompt}}
    };
    return CallOnlineAPIWithMessages(api_url, api_key, model, messages);
}

// ─── P0-2 SSE 流式输出基础设施 ───

// 流式 POST：逐块读取响应，通过回调返回原始数据
// onChunk 返回 false 表示客户端请求中止
inline bool HttpPostStream(const std::wstring& host, int port, const std::wstring& path,
    const std::string& body, const std::string& auth,
    std::function<bool(const char* data, size_t len)> onChunk) {
    HINTERNET hSession = WinHttpOpen(L"MindVault/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    DWORD flags = (port == 443) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    std::wstring headers = L"Content-Type: application/json\r\nAccept: text/event-stream";
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
        return false;
    }

    WinHttpReceiveResponse(hRequest, nullptr);

    bool success = true;
    DWORD bytesAvailable = 0;
    do {
        WinHttpQueryDataAvailable(hRequest, &bytesAvailable);
        if (bytesAvailable > 0) {
            std::vector<char> buffer(bytesAvailable);
            DWORD bytesRead = 0;
            WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead);
            if (bytesRead > 0) {
                if (!onChunk(buffer.data(), bytesRead)) {
                    success = false;  // 客户端中止
                    break;
                }
            }
        }
    } while (bytesAvailable > 0);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return success;
}

// SSE 解析器：处理 OpenAI 兼容的流式响应（data: {...}\n\n）
// 维护内部缓冲区，支持逐块喂入
class SSEParser {
public:
    void feed(const char* data, size_t len, std::function<void(const std::string& delta)> onDelta) {
        buffer_.append(data, len);
        size_t pos;
        // SSE 事件以空行（\n\n）分隔
        while ((pos = buffer_.find("\n\n")) != std::string::npos) {
            std::string event = buffer_.substr(0, pos);
            buffer_.erase(0, pos + 2);
            parseEvent(event, onDelta);
        }
    }

    bool done() const { return done_; }
    int completionTokens() const { return completionTokens_; }

private:
    std::string buffer_;
    bool done_ = false;
    int completionTokens_ = 0;

    void parseEvent(const std::string& event, std::function<void(const std::string& delta)> onDelta) {
        size_t dataPos = event.find("data: ");
        if (dataPos == std::string::npos) return;
        std::string dataStr = event.substr(dataPos + 6);
        // trim
        while (!dataStr.empty() && (dataStr.front() == ' ' || dataStr.front() == '\r' || dataStr.front() == '\n'))
            dataStr.erase(dataStr.begin());
        while (!dataStr.empty() && (dataStr.back() == ' ' || dataStr.back() == '\r' || dataStr.back() == '\n'))
            dataStr.pop_back();

        if (dataStr == "[DONE]") { done_ = true; return; }

        try {
            auto j = nlohmann::json::parse(dataStr);
            if (j.contains("choices") && j["choices"].size() > 0) {
                auto& choice = j["choices"][0];
                // OpenAI 格式：choices[0].delta.content
                if (choice.contains("delta") && choice["delta"].contains("content")) {
                    std::string delta = choice["delta"]["content"].get<std::string>();
                    if (!delta.empty()) onDelta(delta);
                }
                // 部分 API 把 usage 放在 choice 里
                if (choice.contains("usage") && choice["usage"].contains("completion_tokens"))
                    completionTokens_ = choice["usage"]["completion_tokens"].get<int>();
            }
            // usage 可能在顶层
            if (j.contains("usage") && j["usage"].contains("completion_tokens"))
                completionTokens_ = j["usage"]["completion_tokens"].get<int>();
        } catch (...) { /* 忽略解析失败的 chunk */ }
    }
};

// 流式调用结果
struct StreamResult {
    std::string fullContent;
    int completionTokens = 0;
    bool success = false;
    std::string error;
};

// 流式调用在线 API：整合 HttpPostStream + SSEParser
// onDelta: 每收到一个内容增量就调用
// shouldAbort: 检查是否需要中止（客户端断开时返回 true）
inline StreamResult CallOnlineAPIStream(
    const std::string& api_url, const std::string& api_key, const std::string& model,
    const nlohmann::json& messages,
    std::function<void(const std::string& delta)> onDelta,
    std::function<bool()> shouldAbort = []() { return false; }) {
    StreamResult result;

    // 解析 URL
    std::string url = api_url;
    int port = 443;
    if (url.substr(0, 7) == "http://") { url = url.substr(7); port = 80; }
    else if (url.substr(0, 8) == "https://") { url = url.substr(8); port = 443; }

    std::wstring whost(url.begin(), url.end());
    size_t slashPos = whost.find(L'/');
    std::wstring host = whost.substr(0, slashPos);
    std::wstring path = (slashPos != std::wstring::npos) ? whost.substr(slashPos) : L"/";

    // 构造流式请求（stream_options.include_usage=true 确保最后一个 chunk 带 usage）
    nlohmann::json reqBody = {
        {"model", model},
        {"messages", messages},
        {"stream", true},
        {"stream_options", {{"include_usage", true}}}
    };

    SSEParser parser;
    std::string fullContent;

    bool ok = HttpPostStream(host, port, path, reqBody.dump(), api_key,
        [&](const char* data, size_t len) -> bool {
            if (shouldAbort()) return false;
            parser.feed(data, len, [&](const std::string& delta) {
                fullContent += delta;
                onDelta(delta);
            });
            return !parser.done();
        });

    result.fullContent = fullContent;
    result.completionTokens = parser.completionTokens();
    result.success = ok && !fullContent.empty();
    if (!result.success && fullContent.empty()) result.error = "Stream returned empty content";
    return result;
}
#endif

// ─── 构建历史消息（含截断）───
// 策略：从最新往旧累加 token，超预算就停。最后 reverse 恢复正序。
// 这样做的原因是：我们需要保留最近的消息，丢弃最旧的，
// 从后往前遍历 + break 是最自然的写法，最后 reverse 一次即可。
inline nlohmann::json buildHistory(Database& db, int64_t convId, const std::string& systemPrompt, const std::string& ragContext, const std::string& currentQuestion) {
    // 1. 取最近 N 轮（最多 20 条）
    auto msgs = db.GetAIMessages(convId, MAX_HISTORY_TURNS * 2);

    // 2. 固定部分 token 预算
    int fixed = SYSTEM_PROMPT_TOKENS + estimateTokens(systemPrompt) + estimateTokens(ragContext) + estimateTokens(currentQuestion);
    int budget = MAX_CONTEXT_TOKENS - fixed;
    if (budget <= 0) {
        std::cout << "[AI] Warning: budget <= 0, history will be empty" << std::endl;
        return nlohmann::json::array();
    }

    // 3. 从最新往旧累加，直到预算用完
    nlohmann::json kept = nlohmann::json::array();
    int used = 0;
    for (int i = static_cast<int>(msgs.size()) - 1; i >= 0; --i) {
        auto& m = msgs[i];
        int t = m.value("tokens", 0);
        if (t <= 0) t = estimateTokens(m["content"].get<std::string>());
        if (used + t > budget) break;
        kept.push_back(m);
        used += t;
    }

    // 4. 恢复正序
    std::reverse(kept.begin(), kept.end());
    return kept;
}

// ─── 构建发给模型的完整 messages 数组（会话模式）───
inline nlohmann::json buildMessages(const nlohmann::json& history, const std::string& systemPrompt, const std::string& ragContext, const std::string& currentQuestion) {
    nlohmann::json messages = nlohmann::json::array();

    // 1. system prompt
    messages.push_back({{"role", "system"}, {"content", systemPrompt}});

    // 2. 历史对话
    for (auto& h : history) {
        messages.push_back({
            {"role", h["role"]},
            {"content", h["content"]}
        });
    }

    // 3. 当前问题（含 RAG 上下文）
    std::string userContent = "## 参考资料：\n" + ragContext + "\n## 问题：\n" + currentQuestion;
    messages.push_back({{"role", "user"}, {"content", userContent}});

    return messages;
}

// ─── P0-3.1 Query 改写：消解指代，生成独立完整的问题 ───
// 策略：取最近 2 轮历史 + 当前问题，让模型改写为无指代的独立问题。
// 历史不足 2 轮时直接返回原问题（无需改写）。
// 改写失败或 API 不可用时降级返回原问题，不阻塞主流程。
inline std::string QueryRewrite(Database& db, int64_t convId, const std::string& currentQuestion,
    const std::string& provider, const std::string& model, const std::string& api_key, const std::string& api_url) {
    // 1. 取最近 3 轮（6 条）历史
    auto msgs = db.GetAIMessages(convId, 6);

    // 2. 历史不足 2 轮（4 条），不需要改写
    if (msgs.size() < 4) return currentQuestion;

    // 3. 构造改写请求的 messages
    nlohmann::json rewriteMessages = nlohmann::json::array();
    rewriteMessages.push_back({{"role", "system"}, {"content",
        "你是一个查询改写助手。根据对话历史，将用户的当前问题改写为一个独立、完整、无指代的问题。"
        "必须消解所有指代表达（如'它'、'这个'、'那种'、'前者'、'上面提到的'等），"
        "使改写后的问题脱离上下文也能被正确理解。"
        "只输出改写后的问题本身，不要解释、不要加引号、不要加序号。"}});

    // 取最近 2 轮（4 条）历史加入上下文
    size_t start = msgs.size() >= 4 ? msgs.size() - 4 : 0;
    for (size_t i = start; i < msgs.size(); ++i) {
        auto& m = msgs[i];
        rewriteMessages.push_back({{"role", m["role"].get<std::string>()}, {"content", m["content"].get<std::string>()}});
    }

    // 当前问题
    rewriteMessages.push_back({{"role", "user"}, {"content", currentQuestion}});

    // 4. 调用 API 改写
    std::string rewritten = currentQuestion;
#ifdef _WIN32
    if (provider != "ollama" && !api_url.empty() && !api_key.empty()) {
        std::string res = CallOnlineAPIWithMessages(api_url, api_key, model, rewriteMessages);
        if (!res.empty()) {
            try {
                auto r = nlohmann::json::parse(res);
                if (r.contains("choices") && r["choices"].size() > 0) {
                    rewritten = r["choices"][0]["message"]["content"].get<std::string>();
                    // 清理首尾空白、引号、换行
                    while (!rewritten.empty() && (rewritten.front() == ' ' || rewritten.front() == '\n' || rewritten.front() == '\t' || rewritten.front() == '"' || rewritten.front() == '\'' || rewritten.front() == 0xE3)) {
                        if (rewritten.front() == 0xE3 && rewritten.size() >= 3) { rewritten.erase(0, 3); continue; }
                        rewritten.erase(rewritten.begin());
                    }
                    while (!rewritten.empty() && (rewritten.back() == ' ' || rewritten.back() == '\n' || rewritten.back() == '\t' || rewritten.back() == '"' || rewritten.back() == '\'')) {
                        rewritten.pop_back();
                    }
                }
            } catch (const std::exception& e) {
                std::cout << "[AI] QueryRewrite parse error: " << e.what() << std::endl;
            }
        }
    }
#endif

    // 5. 降级：改写结果为空则用原问题
    if (rewritten.empty()) rewritten = currentQuestion;

    std::cout << "[AI] QueryRewrite: '" << currentQuestion << "' -> '" << rewritten << "'" << std::endl;
    return rewritten;
}

inline void RegisterAIRoutes(crow::App<>& app, Database& db) {
    auto search_svc = std::make_shared<services::SearchService>(db);

    // ──────────────────────────────────────────────
    // 会话管理路由
    // ──────────────────────────────────────────────

    // GET /api/ai/conversations - 会话列表
    CROW_ROUTE(app, "/api/ai/conversations").methods("GET"_method)
    ([&db](const crow::request& req) {
        int64_t uid = extractUserId(req);
        if (uid <= 0) return crow::response(401, utils::Error("未登录").dump());
        try {
            auto list = db.GetAIConversations(uid);
            return crow::response(utils::Success(list).dump());
        } catch (const std::exception& e) {
            return crow::response(500, utils::Error(e.what()).dump());
        }
    });

    // POST /api/ai/conversations - 新建会话
    CROW_ROUTE(app, "/api/ai/conversations").methods("POST"_method)
    ([&db](const crow::request& req) {
        int64_t uid = extractUserId(req);
        if (uid <= 0) return crow::response(401, utils::Error("未登录").dump());
        try {
            std::string title = "新对话";
            if (!req.body.empty()) {
                auto body = nlohmann::json::parse(req.body);
                title = body.value("title", title);
            }
            int64_t convId = db.CreateAIConversation(uid, title);
            auto conv = db.GetAIConversation(convId, uid);
            return crow::response(200, utils::Success(conv).dump());
        } catch (const std::exception& e) {
            return crow::response(500, utils::Error(e.what()).dump());
        }
    });

    // GET /api/ai/conversations/:id - 会话详情（含消息历史）
    CROW_ROUTE(app, "/api/ai/conversations/<int>").methods("GET"_method)
    ([&db](const crow::request& req, int convId) {
        int64_t uid = extractUserId(req);
        if (uid <= 0) return crow::response(401, utils::Error("未登录").dump());
        try {
            auto conv = db.GetAIConversation(convId, uid);
            if (conv.is_null()) return crow::response(404, utils::Error("会话不存在").dump());
            auto msgs = db.GetAIMessages(convId, 500);
            conv["messages"] = msgs;
            return crow::response(utils::Success(conv).dump());
        } catch (const std::exception& e) {
            return crow::response(500, utils::Error(e.what()).dump());
        }
    });

    // DELETE /api/ai/conversations/:id - 删除会话（软删）
    CROW_ROUTE(app, "/api/ai/conversations/<int>").methods("DELETE"_method)
    ([&db](const crow::request& req, int convId) {
        int64_t uid = extractUserId(req);
        if (uid <= 0) return crow::response(401, utils::Error("未登录").dump());
        try {
            bool ok = db.SoftDeleteAIConversation(convId, uid);
            if (!ok) return crow::response(404, utils::Error("会话不存在").dump());
            return crow::response(200, utils::Success().dump());
        } catch (const std::exception& e) {
            return crow::response(500, utils::Error(e.what()).dump());
        }
    });

    // PUT /api/ai/conversations/:id - 重命名会话
    CROW_ROUTE(app, "/api/ai/conversations/<int>").methods("PUT"_method)
    ([&db](const crow::request& req, int convId) {
        int64_t uid = extractUserId(req);
        if (uid <= 0) return crow::response(401, utils::Error("未登录").dump());
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string title = body.value("title", "");
            if (title.empty()) return crow::response(400, utils::Error("标题不能为空").dump());
            bool ok = db.UpdateAIConversation(convId, uid, title);
            if (!ok) return crow::response(404, utils::Error("会话不存在").dump());
            return crow::response(200, utils::Success().dump());
        } catch (const std::exception& e) {
            return crow::response(500, utils::Error(e.what()).dump());
        }
    });

    // ──────────────────────────────────────────────
    // POST /api/ai/chat - AI 聊天（兼容单轮 + 多轮会话）
    // ──────────────────────────────────────────────
    CROW_ROUTE(app, "/api/ai/chat").methods("POST"_method)
    ([search_svc, &db](const crow::request& req) {
        try {
            int64_t uid = extractUserId(req);
            auto body = nlohmann::json::parse(req.body);
            std::string question = body.value("question", std::string(""));
            std::string provider = body.value("provider", std::string("ollama"));
            std::string model = body.value("model", std::string("qwen-turbo"));
            std::string api_key = body.value("api_key", std::string(""));
            std::string api_url = body.value("api_url", std::string(""));
            int64_t convId = body.value("conversation_id", int64_t(0));

            std::cout << "[AI] Request: provider=" << provider << ", model=" << model
                      << ", convId=" << convId << ", key=" << (api_key.empty() ? "empty" : "***") << std::endl;

            if (question.empty()) {
                return crow::response(400, utils::Error("Question is required").dump());
            }

            // RAG: 检索相关笔记
            auto results = search_svc->HybridSearch(question, 5, api_url, api_key, model);

            // 构建检索上下文
            std::string ragContext;
            for (size_t i = 0; i < results.size(); ++i) {
                auto& r = results[i];
                std::string title = r.value("title", std::string(""));
                std::string snippet = r.value("content_highlight", std::string(""));
                if (snippet.empty()) snippet = r.value("title_highlight", std::string(""));
                size_t pos;
                while ((pos = snippet.find(">>>")) != std::string::npos) snippet.replace(pos, 3, "");
                while ((pos = snippet.find("<<<")) != std::string::npos) snippet.replace(pos, 3, "");
                ragContext += "[来源" + std::to_string(i + 1) + ": " + title + "]\n" + snippet + "\n\n";
            }

            std::string systemPrompt = "你是一个知识库助手。请基于参考资料回答问题。如果资料无法回答，请明确说明。";
            std::string answer;
            int64_t msgId = 0;

            // ── 会话模式 ──
            if (convId > 0 && uid > 0) {
                std::mutex& mtx = getConvMutex(convId);
                std::lock_guard<std::mutex> lock(mtx);

                db.Begin();
                try {
                    // 1. 校验归属
                    auto conv = db.GetAIConversation(convId, uid);
                    if (conv.is_null()) {
                        db.Rollback();
                        return crow::response(404, utils::Error("会话不存在或无权访问").dump());
                    }

                    // 2. 写入用户消息
                    db.AddAIMessage(convId, "user", question, estimateTokens(question));

                    // 2.5 P0-3.1 Query 改写：用历史消解指代，生成独立完整的问题
                    // 改写失败时降级返回原问题，不阻塞主流程
                    std::cout << "[AI Stream] Starting QueryRewrite..." << std::endl;
            std::string effectiveQuestion = QueryRewrite(db, convId, question, provider, model, api_key, api_url);
            std::cout << "[AI Stream] QueryRewrite done, changed=" << (effectiveQuestion != question) << std::endl;

                    // 用改写后的问题重新检索（如果改写结果不同）
                    if (effectiveQuestion != question) {
                        results = search_svc->HybridSearch(effectiveQuestion, 5, api_url, api_key, model);
                        ragContext.clear();
                        for (size_t i = 0; i < results.size(); ++i) {
                            auto& r = results[i];
                            std::string title = r.value("title", std::string(""));
                            std::string snippet = r.value("content_highlight", std::string(""));
                            if (snippet.empty()) snippet = r.value("title_highlight", std::string(""));
                            size_t pos;
                            while ((pos = snippet.find(">>>")) != std::string::npos) snippet.replace(pos, 3, "");
                            while ((pos = snippet.find("<<<")) != std::string::npos) snippet.replace(pos, 3, "");
                            ragContext += "[来源" + std::to_string(i + 1) + ": " + title + "]\n" + snippet + "\n\n";
                        }
                        std::cout << "[AI] Re-searched with rewritten query" << std::endl;
                    }

                    // 3. 读取历史（截断）—— 用改写后的问题计算 token 预算
                    auto history = buildHistory(db, convId, systemPrompt, ragContext, effectiveQuestion);

                    // 4. 拼装 messages 数组 —— 用改写后的问题
                    auto messages = buildMessages(history, systemPrompt, ragContext, effectiveQuestion);

                    // 5. 调用模型
                    int completionTokens = 0;  // P0-4: 用于 token 校准（在线 API 返回 usage 时填充）
#ifdef _WIN32
                    if (provider == "ollama") {
                        nlohmann::json ollama_req = {{"model", model}, {"prompt", systemPrompt + "\n\n" + question}, {"stream", false}};
                        std::string res = HttpPost(L"localhost", 11434, L"/api/generate", ollama_req.dump());
                        if (!res.empty()) {
                            auto r = nlohmann::json::parse(res);
                            answer = r.value("response", "");
                        }
                    } else {
                        if (api_url.empty()) {
                            if (provider == "tongyi") api_url = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
                            else if (provider == "doubao") api_url = "https://ark.cn-beijing.volces.com/api/v3/chat/completions";
                            else if (provider == "openai") api_url = "https://api.openai.com/v1/chat/completions";
                        }
                        if (!api_url.empty() && !api_key.empty()) {
                            std::string res = CallOnlineAPIWithMessages(api_url, api_key, model, messages);
                            if (!res.empty()) {
                                try {
                                    auto r = nlohmann::json::parse(res);
                                    if (r.contains("choices") && r["choices"].size() > 0)
                                        answer = r["choices"][0]["message"]["content"];
                                    else if (r.contains("error"))
                                        answer = "API 错误: " + r["error"].value("message", "Unknown");
                                    // P0-4: 提取 usage.completion_tokens 用于校准
                                    if (r.contains("usage") && r["usage"].contains("completion_tokens")) {
                                        completionTokens = r["usage"]["completion_tokens"].get<int>();
                                        std::cout << "[AI] usage: completion_tokens=" << completionTokens << std::endl;
                                    }
                                } catch (const std::exception& e) {
                                    answer = "API 返回格式错误: " + res.substr(0, 200);
                                }
                            } else { answer = "API 请求失败，请检查网络连接"; }
                        } else { answer = "请配置 API Key"; }
                    }
#endif

                    if (answer.empty()) {
                        answer = "无法获取 AI 回答。请检查 API Key 和网络连接。\n\n以下是 RAG 检索结果：\n\n" + ragContext;
                    }

                    // 6. 写入 assistant 消息（先用字符粗估，后续用 usage 校准）
                    msgId = db.AddAIMessage(convId, "assistant", answer, estimateTokens(answer));

                    // P0-4: 用 API 返回的 completion_tokens 校准（比字符粗估更准确）
                    // 为什么不用精确 tokenizer：1) 引入额外依赖增加体积 2) 不同模型 tokenizer 不同
                    // 3) usage 字段是模型侧精确统计，直接用它校准是最可靠的方式
                    if (completionTokens > 0 && msgId > 0) {
                        db.UpdateAIMessageTokens(msgId, completionTokens);
                        std::cout << "[AI] Calibrated assistant message tokens: " << completionTokens << std::endl;
                    }

                    // 7. 更新会话标题（首轮自动截取前 20 字）
                    if (conv.value("title", "") == "新对话" || conv.value("title", "").empty()) {
                        std::string newTitle = question.length() > 20 ? question.substr(0, 20) : question;
                        db.UpdateAIConversation(convId, uid, newTitle);
                    }

                    // 8. 更新 updated_at
                    db.Execute(
                        "UPDATE ai_conversations SET updated_at = datetime('now','localtime') WHERE id = ?",
                        {convId}
                    );

                    db.Commit();
                } catch (...) {
                    db.Rollback();
                    throw;
                }

            } else {
                // ── 单轮兼容模式（现有逻辑，不写会话表）──
                std::string prompt = "## 参考资料：\n" + ragContext + "\n## 问题：\n" + question;
#ifdef _WIN32
                if (provider == "ollama") {
                    nlohmann::json ollama_req = {{"model", model}, {"prompt", "你是一个知识库助手。\n\n" + prompt}, {"stream", false}};
                    std::string res = HttpPost(L"localhost", 11434, L"/api/generate", ollama_req.dump());
                    if (!res.empty()) {
                        auto r = nlohmann::json::parse(res);
                        answer = r.value("response", "");
                    }
                } else {
                    if (api_url.empty()) {
                        if (provider == "tongyi") api_url = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
                        else if (provider == "doubao") api_url = "https://ark.cn-beijing.volces.com/api/v3/chat/completions";
                        else if (provider == "openai") api_url = "https://api.openai.com/v1/chat/completions";
                    }
                    if (!api_url.empty() && !api_key.empty()) {
                        std::string res = CallOnlineAPI(api_url, api_key, model, prompt);
                        if (!res.empty()) {
                            try {
                                auto r = nlohmann::json::parse(res);
                                if (r.contains("choices") && r["choices"].size() > 0)
                                    answer = r["choices"][0]["message"]["content"];
                                else if (r.contains("error"))
                                    answer = "API 错误: " + r["error"].value("message", "Unknown");
                            } catch (const std::exception& e) {
                                answer = "API 返回格式错误: " + res.substr(0, 200);
                            }
                        } else { answer = "API 请求失败，请检查网络连接"; }
                    } else { answer = "请配置 API Key"; }
                }
#endif
                if (answer.empty()) {
                    answer = "无法获取 AI 回答。请检查 API Key 和网络连接。\n\n以下是 RAG 检索结果：\n\n" + ragContext;
                }
            }

            nlohmann::json response = {
                {"answer", answer},
                {"sources", results},
                {"provider", provider},
                {"model", model},
                {"conversation_id", convId},
                {"message_id", msgId}
            };
            return crow::response(utils::Success(response).dump());
        } catch (const std::exception& e) {
            std::cout << "[AI] Error: " << e.what() << std::endl;
            std::string msg = e.what();
            // 数据库忙时返回友好提示，不暴露技术细节
            if (msg.find("SQL error") != std::string::npos || msg.find("SQLITE_BUSY") != std::string::npos) {
                return crow::response(500, utils::Error("服务繁忙，请稍后重试").dump());
            }
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // ──────────────────────────────────────────────
    // P0-2 SSE 流式输出端点
    // ──────────────────────────────────────────────
    // POST /api/ai/chat/stream - 流式对话（SSE）
    // 与 /api/ai/chat 共享会话机制、QueryRewrite、token 预算
    // 前端用 fetch + ReadableStream 逐块渲染（EventSource 不支持 POST）
    CROW_ROUTE(app, "/api/ai/chat/stream").methods("POST"_method)
    ([search_svc, &db](const crow::request& req, crow::response& res) {
        std::atomic<bool> aborted{false};
        bool streamStarted = false;  // 是否已发送 HTTP 响应头
        try {
            int64_t uid = extractUserId(req);
            auto body = nlohmann::json::parse(req.body);
            std::string question = body.value("question", std::string(""));
            std::string provider = body.value("provider", std::string("ollama"));
            std::string model = body.value("model", std::string("qwen-turbo"));
            std::string api_key = body.value("api_key", std::string(""));
            std::string api_url = body.value("api_url", std::string(""));
            int64_t convId = body.value("conversation_id", int64_t(0));

            std::cout << "[AI Stream] Request: provider=" << provider << ", model=" << model
                      << ", convId=" << convId << ", q=" << question.substr(0, 50) << std::endl;

            if (question.empty()) {
                res.code = 400;
                res.write(utils::Error("Question is required").dump());
                res.end();
                return;
            }
            if (convId <= 0 || uid <= 0) {
                res.code = 400;
                res.write(utils::Error("流式对话需要 conversation_id").dump());
                res.end();
                return;
            }

            // RAG 检索
            std::cout << "[AI Stream] Starting RAG search..." << std::endl;
            auto results = search_svc->HybridSearch(question, 5, api_url, api_key, model);
            std::cout << "[AI Stream] RAG search done, results=" << results.size() << std::endl;
            std::string ragContext;
            for (size_t i = 0; i < results.size(); ++i) {
                auto& r = results[i];
                std::string title = r.value("title", std::string(""));
                std::string snippet = r.value("content_highlight", std::string(""));
                if (snippet.empty()) snippet = r.value("title_highlight", std::string(""));
                size_t pos;
                while ((pos = snippet.find(">>>")) != std::string::npos) snippet.replace(pos, 3, "");
                while ((pos = snippet.find("<<<")) != std::string::npos) snippet.replace(pos, 3, "");
                ragContext += "[来源" + std::to_string(i + 1) + ": " + title + "]\n" + snippet + "\n\n";
            }

            std::string systemPrompt = "你是一个知识库助手。请基于参考资料回答问题。如果资料无法回答，请明确说明。";
            std::string answer;
            int64_t msgId = 0;
            int completionTokens = 0;

            std::mutex& mtx = getConvMutex(convId);
            std::lock_guard<std::mutex> lock(mtx);

            std::cout << "[AI Stream] Starting DB transaction..." << std::endl;
            db.Begin();
            std::cout << "[AI Stream] DB transaction started" << std::endl;

            // 1. 校验归属
            auto conv = db.GetAIConversation(convId, uid);
            if (conv.is_null()) {
                db.Rollback();
                res.code = 404;
                res.write(utils::Error("会话不存在或无权访问").dump());
                res.end();
                return;
            }

            // 2. 写入用户消息
            db.AddAIMessage(convId, "user", question, estimateTokens(question));

            // 3. P0-3.1 Query 改写
            std::string effectiveQuestion = QueryRewrite(db, convId, question, provider, model, api_key, api_url);
            if (effectiveQuestion != question) {
                results = search_svc->HybridSearch(effectiveQuestion, 5, api_url, api_key, model);
                ragContext.clear();
                for (size_t i = 0; i < results.size(); ++i) {
                    auto& r = results[i];
                    std::string title = r.value("title", std::string(""));
                    std::string snippet = r.value("content_highlight", std::string(""));
                    if (snippet.empty()) snippet = r.value("title_highlight", std::string(""));
                    size_t pos;
                    while ((pos = snippet.find(">>>")) != std::string::npos) snippet.replace(pos, 3, "");
                    while ((pos = snippet.find("<<<")) != std::string::npos) snippet.replace(pos, 3, "");
                    ragContext += "[来源" + std::to_string(i + 1) + ": " + title + "]\n" + snippet + "\n\n";
                }
            }

            // 4. 读取历史（P0-4 token 预算截断）
            std::cout << "[AI Stream] Building history..." << std::endl;
            auto history = buildHistory(db, convId, systemPrompt, ragContext, effectiveQuestion);
            std::cout << "[AI Stream] History built, turns=" << history.size() << std::endl;
            auto messages = buildMessages(history, systemPrompt, ragContext, effectiveQuestion);

            // 5. 建立 SSE 流
            if (!res.stream_sink_ || !res.stream_close_) {
                db.Rollback();
                res.code = 500;
                res.write(utils::Error("Streaming not supported").dump());
                res.end();
                return;
            }

            auto origSink = res.stream_sink_;
            res.stream_sink_ = [&](const std::string& data) {
                if (aborted.load()) return;
                try { origSink(data); }
                catch (...) { aborted.store(true); std::cout << "[AI Stream] Client disconnected" << std::endl; }
            };

            // 手动写 HTTP 响应头
            std::string httpHeader =
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/event-stream\r\n"
                "Cache-Control: no-cache\r\n"
                "Connection: keep-alive\r\n"
                "X-Accel-Buffering: no\r\n"
                "\r\n";
            res.stream_sink_(httpHeader);
            streamStarted = true;
            std::cout << "[AI Stream] HTTP header sent, stream started" << std::endl;

            nlohmann::json startEvt = {{"type", "start"}, {"conversation_id", convId}};
            res.stream_sink_("data: " + startEvt.dump() + "\n\n");

            // 6. 调用流式 API
#ifdef _WIN32
            if (provider != "ollama" && !api_key.empty()) {
                if (api_url.empty()) {
                    if (provider == "tongyi") api_url = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
                    else if (provider == "doubao") api_url = "https://ark.cn-beijing.volces.com/api/v3/chat/completions";
                    else if (provider == "openai") api_url = "https://api.openai.com/v1/chat/completions";
                }

                StreamResult sr = CallOnlineAPIStream(
                    api_url, api_key, model, messages,
                    [&](const std::string& delta) {
                        nlohmann::json evt = {{"type", "delta"}, {"content", delta}};
                        res.stream_sink_("data: " + evt.dump() + "\n\n");
                    },
                    [&]() -> bool { return aborted.load(); }
                );
                answer = sr.fullContent;
                completionTokens = sr.completionTokens;
                if (!sr.success && !aborted.load()) {
                    nlohmann::json errEvt = {{"type", "error"}, {"message", sr.error.empty() ? "API 请求失败" : sr.error}};
                    res.stream_sink_("data: " + errEvt.dump() + "\n\n");
                }
            } else {
                nlohmann::json errEvt = {{"type", "error"}, {"message", "请配置 API Key（流式暂不支持 ollama）"}};
                res.stream_sink_("data: " + errEvt.dump() + "\n\n");
            }
#else
            nlohmann::json errEvt = {{"type", "error"}, {"message": "流式输出仅支持 Windows 平台"}};
            res.stream_sink_("data: " + errEvt.dump() + "\n\n");
#endif

            // 7. 写入 assistant 消息（即使客户端断开也写入）
            if (answer.empty() && !aborted.load()) {
                answer = "无法获取 AI 回答。请检查 API Key 和网络连接。\n\n以下是 RAG 检索结果：\n\n" + ragContext;
            }
            if (!answer.empty()) {
                msgId = db.AddAIMessage(convId, "assistant", answer, estimateTokens(answer));
                if (completionTokens > 0 && msgId > 0) {
                    db.UpdateAIMessageTokens(msgId, completionTokens);
                }
            }

            // 8. 更新会话标题（首轮）
            if (conv.value("title", "") == "新对话" || conv.value("title", "").empty()) {
                std::string newTitle = question.length() > 20 ? question.substr(0, 20) : question;
                db.UpdateAIConversation(convId, uid, newTitle);
            }
            db.Execute("UPDATE ai_conversations SET updated_at = datetime('now','localtime') WHERE id = ?", {convId});
            db.Commit();

            // 9. 发送结束事件
            if (!aborted.load()) {
                nlohmann::json doneEvt = {{"type", "done"}, {"message_id", msgId}, {"tokens", completionTokens}};
                res.stream_sink_("data: " + doneEvt.dump() + "\n\n");
            }

            // 10. 关闭连接
            if (res.stream_close_) res.stream_close_();

        } catch (const std::exception& e) {
            std::cout << "[AI Stream] Exception: " << e.what() << std::endl;
            try { db.Rollback(); } catch (...) {}
            if (streamStarted && !aborted.load() && res.stream_sink_) {
                // 流已建立：发送 SSE 错误事件
                try {
                    nlohmann::json errEvt = {{"type", "error"}, {"message", e.what()}};
                    res.stream_sink_("data: " + errEvt.dump() + "\n\n");
                    if (res.stream_close_) res.stream_close_();
                } catch (...) {}
            } else {
                // 流未建立：用普通 HTTP 500 响应
                try {
                    res.code = 500;
                    res.write(utils::Error(e.what()).dump());
                    res.end();
                } catch (...) {}
            }
        }
    });

    // ──────────────────────────────────────────────
    // P1-6 内容创作套件 + P1-7 自动标签：统一 action 接口
    // ──────────────────────────────────────────────

    // POST /api/ai/action - 润色/扩写/总结/翻译/大纲/自动标签
    CROW_ROUTE(app, "/api/ai/action").methods("POST"_method)
    ([&db](const crow::request& req) {
        int64_t uid = extractUserId(req);
        if (uid <= 0) return crow::response(401, utils::Error("未登录").dump());
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string action = body.value("action", std::string(""));
            std::string text = body.value("text", std::string(""));
            std::string target_lang = body.value("target_lang", std::string(""));
            int64_t note_id = body.value("note_id", int64_t(0));
            std::string api_url = body.value("api_url", std::string(""));
            std::string api_key = body.value("api_key", std::string(""));
            std::string model = body.value("model", std::string(""));

            if (action.empty()) return crow::response(400, utils::Error("action is required").dump());
            if (text.empty()) return crow::response(400, utils::Error("text is required").dump());

            // 合法 action 白名单
            static const std::vector<std::string> valid_actions = {"polish", "expand", "summarize", "translate", "outline", "tags"};
            if (std::find(valid_actions.begin(), valid_actions.end(), action) == valid_actions.end()) {
                return crow::response(400, utils::Error("unsupported action: " + action).dump());
            }

            if (api_key.empty()) {
                return crow::response(400, utils::Error("请配置 API Key").dump());
            }

            auto user_db = GetUserDb2(req);
            if (!user_db) return crow::response(500, utils::Error("用户库不存在").dump());

            // 构造 AICaller（复用 CallOnlineAPIWithMessages）
            services::AICaller caller = [](const std::string& url, const std::string& key,
                                             const std::string& mdl, const nlohmann::json& msgs) {
                return CallOnlineAPIWithMessages(url, key, mdl, msgs);
            };

            services::AIActionService svc(*user_db, caller);
            auto result = svc.ExecuteAction(action, text, target_lang, note_id, api_url, api_key, model);
            return crow::response(utils::Success(result).dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // POST /api/ai/graph-qa - P2-1 知识图谱问答（结构化走SQL，非结构化走LLM）
    CROW_ROUTE(app, "/api/ai/graph-qa").methods("POST"_method)
    ([&db](const crow::request& req) {
        int64_t uid = extractUserId(req);
        if (uid <= 0) return crow::response(401, utils::Error("未登录").dump());
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string question = body.value("question", std::string(""));
            int64_t note_id = body.value("note_id", int64_t(0));
            std::string api_url = body.value("api_url", std::string(""));
            std::string api_key = body.value("api_key", std::string(""));
            std::string model = body.value("model", std::string(""));

            if (question.empty()) return crow::response(400, utils::Error("question is required").dump());

            auto user_db = GetUserDb2(req);
            if (!user_db) return crow::response(500, utils::Error("用户库不存在").dump());

            services::GraphAICaller caller = [](const std::string& url, const std::string& key,
                                                 const std::string& mdl, const nlohmann::json& msgs) {
                return CallOnlineAPIWithMessages(url, key, mdl, msgs);
            };

            services::GraphQAService svc(*user_db, caller);
            auto result = svc.Answer(question, note_id, api_url, api_key, model);
            return crow::response(utils::Success(result).dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // POST /api/ai/chat-with-tools - P2-2 function calling 轻量用法（2个真实只读工具）
    CROW_ROUTE(app, "/api/ai/chat-with-tools").methods("POST"_method)
    ([&db](const crow::request& req) {
        int64_t uid = extractUserId(req);
        if (uid <= 0) return crow::response(401, utils::Error("未登录").dump());
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string question = body.value("question", std::string(""));
            std::string api_url = body.value("api_url", std::string(""));
            std::string api_key = body.value("api_key", std::string(""));
            std::string model = body.value("model", std::string(""));

            if (question.empty()) return crow::response(400, utils::Error("question is required").dump());
            if (api_key.empty()) return crow::response(400, utils::Error("请配置 API Key").dump());

            auto user_db = GetUserDb2(req);
            if (!user_db) return crow::response(500, utils::Error("用户库不存在").dump());

            services::FCAICaller caller = [](const std::string& url, const std::string& key,
                                              const std::string& mdl, const nlohmann::json& msgs,
                                              const nlohmann::json& tools) {
                return CallOnlineAPIWithTools(url, key, mdl, msgs, tools);
            };

            services::FunctionCallingService svc(*user_db, caller);
            auto result = svc.ChatWithTools(question, api_url, api_key, model);
            return crow::response(utils::Success(result).dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // GET /api/ai/status - 检查 AI 提供商
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
}

} // namespace mindvault::routes
