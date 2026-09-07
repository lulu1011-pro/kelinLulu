#pragma once
// P1-5 混合检索：笔记切块 + embedding API + 余弦相似度 + 向量搜索
// header-only，与现有 service 风格一致

#include "../database.h"
#include <string>
#include <vector>
#include <cmath>
#include <nlohmann/json.hpp>
#include <iostream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace mindvault::services {

// ─── 切块常量 ───
constexpr int CHUNK_MAX_SIZE = 800;       // 单块最大字符数
constexpr int CHUNK_OVERLAP = 50;          // 相邻块重叠字符数（防切断语义）
constexpr int CHUNK_MIN_SIZE = 50;          // 少于此长度的块跳过
constexpr int EMBEDDING_TIMEOUT_MS = 20000;// embedding API 超时（批量请求较慢）
constexpr int EMBED_BATCH_SIZE = 16;        // 单次请求同时嵌入的文本条数（智谱上限 64，留余量）

class ChunkService {
public:
    explicit ChunkService(Database& db) : db_(db) {}

    // ─── 切块：按标题→段落→窗口切分 ───
    static std::vector<std::string> SplitIntoChunks(const std::string& title, const std::string& content) {
        std::vector<std::string> chunks;

        // 把标题拼到内容前面，保证 embedding 包含上下文
        std::string full_text;
        if (!title.empty()) {
            full_text = "# " + title + "\n\n" + content;
        } else {
            full_text = content;
        }

        // 第 1 步：按 Markdown 标题切分（# / ## / ###）
        std::vector<std::string> sections;
        std::string current;
        size_t pos = 0;
        while (pos < full_text.size()) {
            // 检测行首的 # 标题
            if (full_text[pos] == '#' && (pos == 0 || full_text[pos - 1] == '\n')) {
                if (!current.empty()) {
                    sections.push_back(current);
                    current.clear();
                }
                // 读到行尾
                size_t eol = full_text.find('\n', pos);
                if (eol == std::string::npos) eol = full_text.size();
                current = full_text.substr(pos, eol - pos) + "\n";
                pos = eol + 1;
            } else {
                current += full_text[pos];
                pos++;
            }
        }
        if (!current.empty()) sections.push_back(current);

        // 第 2 步：每个 section 按空行段落切，超长段再按窗口切
        for (const auto& section : sections) {
            std::vector<std::string> paragraphs = SplitByParagraph(section);
            for (const auto& para : paragraphs) {
                if (para.size() < CHUNK_MIN_SIZE) continue;
                if (para.size() <= CHUNK_MAX_SIZE) {
                    chunks.push_back(para);
                } else {
                    // 超长段：按窗口切，重叠 50 字符
                    auto window_chunks = SplitByWindow(para, CHUNK_MAX_SIZE, CHUNK_OVERLAP);
                    for (auto& wc : window_chunks) {
                        if (wc.size() >= CHUNK_MIN_SIZE) chunks.push_back(wc);
                    }
                }
            }
        }

        return chunks;
    }

    // ─── 重建某篇笔记的 chunk（删旧→切块→存库→批量调 embedding）───
    // 返回成功嵌入的 chunk 数（0 = 未配置 embedding 或接口失败）
    // 批量嵌入（每组 EMBED_BATCH_SIZE 条）避免几千条逐条请求拖慢重建
    int RebuildChunksForNote(int64_t note_id, const std::string& title, const std::string& content,
                                const std::string& api_url = "", const std::string& api_key = "", const std::string& model = "") {
        int embedded = 0;
        try {
            // 删旧 chunk
            db_.DeleteChunksByNote(note_id);

            // 切块
            auto chunks = SplitIntoChunks(title, content);
            if (chunks.empty()) return 0;

            // 配置齐全：批量调 embedding 后入库
            if (!api_key.empty() && !api_url.empty() && !model.empty()) {
                for (size_t i = 0; i < chunks.size(); i += EMBED_BATCH_SIZE) {
                    size_t end = std::min(chunks.size(), i + EMBED_BATCH_SIZE);
                    std::vector<std::string> group(chunks.begin() + i, chunks.begin() + end);
                    auto embs = EmbedTextsBatch(api_url, api_key, model, group);
                    for (size_t j = 0; j < group.size(); ++j) {
                        std::string embedding_json;
                        if (j < embs.size() && !embs[j].empty()) {
                            embedding_json = nlohmann::json(embs[j]).dump();
                            ++embedded;
                        }
                        db_.AddChunk(note_id, static_cast<int>(i + j), group[j], embedding_json);
                    }
                }
                std::cout << "[Chunk] Rebuilt note " << note_id << ": " << chunks.size() << " chunks, embedded " << embedded << std::endl;
            } else {
                // 未配置 embedding：只存切块文本（向量路自动跳过，关键词路不受影响）
                for (size_t i = 0; i < chunks.size(); ++i) {
                    db_.AddChunk(note_id, static_cast<int>(i), chunks[i], "");
                }
                std::cout << "[Chunk] Rebuilt note " << note_id << " (text-only): " << chunks.size() << " chunks" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "[Chunk] RebuildChunksForNote failed: " << e.what() << std::endl;
            // 失败不影响笔记保存，chunk 可能不完整，下次更新时重建
        }
        return embedded;
    }

    // ─── 全量重建：所有未删除笔记重新切块并批量嵌入 ───
    // 场景：首次配好 embedding 模型 / 更换 embedding 模型（维度变了必须重建）
    // 返回 {notes, chunks, api_error}；api_error=true 表示首篇就全失败（多半是模型名/Key/余额问题），已提前中止
    nlohmann::json RebuildAllEmbeddings(const std::string& api_url, const std::string& api_key, const std::string& model) {
        nlohmann::json out = {{"notes", 0}, {"chunks", 0}, {"api_error", false}};
        auto notes = db_.Query(
            "SELECT id, title, content FROM notes WHERE is_deleted = 0 ORDER BY updated_at DESC"
        );
        for (auto& n : notes) {
            int64_t id = n["id"].get<int64_t>();
            std::string title = n.value("title", std::string(""));
            std::string content = n.value("content", std::string(""));
            int got = RebuildChunksForNote(id, title, content, api_url, api_key, model);
            int notesDone = out["notes"].get<int>() + 1;
            out["notes"] = notesDone;
            out["chunks"] = out["chunks"].get<int>() + got;
            // 第一篇有内容的笔记一个都没嵌上 → 大概率配置/余额问题，及时止损不空等几千次
            if (notesDone == 1 && got == 0 && !content.empty()) {
                std::cerr << "[Chunk] embedding API failed on first note, aborting reindex" << std::endl;
                out["api_error"] = true;
                break;
            }
        }
        return out;
    }

    // ─── 向量搜索：读所有 chunk，内存算余弦，返回 topN ───
    // 返回格式：[{note_id, chunk_text, similarity, title, folder}]
    nlohmann::json VectorSearch(const std::vector<double>& query_embedding, int top_n = 20) {
        nlohmann::json results = nlohmann::json::array();
        if (query_embedding.empty()) return results;

        auto all_chunks = db_.GetAllChunks();
        if (all_chunks.empty()) return results;

        std::vector<std::pair<double, nlohmann::json>> scored; // (similarity, chunk_json)

        for (auto& chunk : all_chunks) {
            std::string emb_str = chunk.value("embedding_json", std::string(""));
            if (emb_str.empty()) continue;

            std::vector<double> chunk_emb;
            try {
                nlohmann::json j = nlohmann::json::parse(emb_str);
                if (!j.is_array()) continue;
                for (auto& v : j) {
                    chunk_emb.push_back(v.get<double>());
                }
            } catch (...) {
                continue; // 解析失败跳过
            }

            if (chunk_emb.size() != query_embedding.size()) {
                continue; // 维度不一致跳过（换模型后需重建）
            }

            double sim = CosineSimilarity(query_embedding, chunk_emb);
            nlohmann::json item;
            item["note_id"] = chunk["note_id"];
            item["chunk_text"] = chunk["chunk_text"];
            item["similarity"] = sim;
            item["title"] = chunk.value("title", std::string(""));
            item["folder"] = chunk.value("folder", std::string(""));
            scored.push_back({sim, item});
        }

        // 按相似度降序排序
        std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });

        int count = std::min(top_n, (int)scored.size());
        for (int i = 0; i < count; ++i) {
            results.push_back(scored[i].second);
        }

        return results;
    }

    // ─── 余弦相似度 ───
    static double CosineSimilarity(const std::vector<double>& a, const std::vector<double>& b) {
        if (a.size() != b.size() || a.empty()) return 0.0;
        double dot = 0.0, norm_a = 0.0, norm_b = 0.0;
        for (size_t i = 0; i < a.size(); ++i) {
            dot += a[i] * b[i];
            norm_a += a[i] * a[i];
            norm_b += b[i] * b[i];
        }
        if (norm_a == 0.0 || norm_b == 0.0) return 0.0;
        return dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
    }

    // ─── 调 embedding API（OpenAI 兼容格式，一次请求多条文本）───
    // POST {api_url}，body {"model": model, "input": [文本数组]}
    // 返回与 texts 顺序对齐的向量列表；整组失败返回空（外层据此判定 API 不可用）
    static std::vector<std::vector<double>> EmbedTextsBatch(
        const std::string& api_url, const std::string& api_key,
        const std::string& model, const std::vector<std::string>& texts) {
        std::vector<std::vector<double>> result;
        if (texts.empty()) return result;
#ifdef _WIN32
        if (api_url.empty() || api_key.empty() || model.empty()) return result;

        // 解析 URL
        std::string url = api_url;
        int port = 443;
        bool is_https = true;
        if (url.substr(0, 7) == "http://") { url = url.substr(7); is_https = false; port = 80; }
        else if (url.substr(0, 8) == "https://") { url = url.substr(8); }

        auto slash_pos = url.find('/');
        std::string host_str = (slash_pos != std::string::npos) ? url.substr(0, slash_pos) : url;
        std::wstring path = (slash_pos != std::string::npos)
            ? std::wstring(url.begin() + slash_pos, url.end())
            : L"/v1/embeddings";

        auto colon_pos = host_str.find(':');
        if (colon_pos != std::string::npos) {
            port = std::stoi(host_str.substr(colon_pos + 1));
            host_str = host_str.substr(0, colon_pos);
        }
        std::wstring host(host_str.begin(), host_str.end());

        // 构造请求体：input 传字符串数组实现批量
        nlohmann::json body;
        body["model"] = model;
        body["input"] = texts;
        std::string body_str = body.dump();

        // WinHTTP 调用（带超时）
        HINTERNET hSession = WinHttpOpen(L"MindVault/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
        if (!hSession) return result;

        WinHttpSetTimeouts(hSession, EMBEDDING_TIMEOUT_MS, EMBEDDING_TIMEOUT_MS,
                            EMBEDDING_TIMEOUT_MS, EMBEDDING_TIMEOUT_MS);

        HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), (INTERNET_PORT)port, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

        DWORD flags = is_https ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) {
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return result;
        }

        std::wstring headers = L"Content-Type: application/json\r\nAuthorization: Bearer ";
        headers += std::wstring(api_key.begin(), api_key.end());

        BOOL send_ok = WinHttpSendRequest(hRequest, headers.c_str(), -1,
            (LPVOID)body_str.c_str(), (DWORD)body_str.size(), (DWORD)body_str.size(), 0);
        if (!send_ok) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return result;
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

        // 解析响应 data[i].embedding（按 data 顺序对齐输入）
        try {
            nlohmann::json j = nlohmann::json::parse(response);
            if (j.contains("error")) {
                std::cerr << "[Chunk] EmbedTexts API error: "
                          << j["error"].value("message", std::string("unknown")).substr(0, 200) << std::endl;
                return result;
            }
            if (!j.contains("data") || !j["data"].is_array()) return result;
            result.resize(texts.size());
            for (auto& d : j["data"]) {
                int idx = d.value("index", -1);
                if (idx < 0 || idx >= (int)result.size()) continue;
                if (!d.contains("embedding") || !d["embedding"].is_array()) continue;
                for (auto& v : d["embedding"]) result[idx].push_back(v.get<double>());
            }
        } catch (...) {
            std::cerr << "[Chunk] EmbedTexts parse failed: " << response.substr(0, 200) << std::endl;
        }
#else
        // 非 Windows 平台暂不支持 embedding
        (void)api_url; (void)api_key; (void)model; (void)texts;
#endif
        return result;
    }

    // ─── 单条 embedding（查询向量用）───
    // 失败返回空 vector
    static std::vector<double> EmbedText(const std::string& api_url, const std::string& api_key,
                                           const std::string& model, const std::string& text) {
        auto batch = EmbedTextsBatch(api_url, api_key, model, {text});
        if (!batch.empty()) return batch[0];
        return {};
    }

private:
    Database& db_;

    // 按空行段落切分
    static std::vector<std::string> SplitByParagraph(const std::string& text) {
        std::vector<std::string> paragraphs;
        std::string current;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n' && i + 1 < text.size() && text[i + 1] == '\n') {
                if (!current.empty()) {
                    // 去除首尾空白
                    size_t s = current.find_first_not_of(" \t\r\n");
                    size_t e = current.find_last_not_of(" \t\r\n");
                    if (s != std::string::npos) {
                        paragraphs.push_back(current.substr(s, e - s + 1));
                    }
                    current.clear();
                }
                i++; // 跳过第二个换行
            } else {
                current += text[i];
            }
        }
        if (!current.empty()) {
            size_t s = current.find_first_not_of(" \t\r\n");
            size_t e = current.find_last_not_of(" \t\r\n");
            if (s != std::string::npos) {
                paragraphs.push_back(current.substr(s, e - s + 1));
            }
        }
        return paragraphs;
    }

    // 按窗口切分（带重叠）
    static std::vector<std::string> SplitByWindow(const std::string& text, int window_size, int overlap) {
        std::vector<std::string> chunks;
        if ((int)text.size() <= window_size) {
            chunks.push_back(text);
            return chunks;
        }
        int step = window_size - overlap;
        if (step <= 0) step = window_size; // 防止 overlap >= window_size
        for (int start = 0; start < (int)text.size(); start += step) {
            int end = std::min(start + window_size, (int)text.size());
            // 尽量在句子边界切（找最近的句号/问号/感叹号）
            if (end < (int)text.size()) {
                int boundary = FindSentenceBoundary(text, start + overlap, end);
                if (boundary > start) end = boundary;
            }
            std::string chunk = text.substr(start, end - start);
            chunks.push_back(chunk);
            if (end >= (int)text.size()) break;
        }
        return chunks;
    }

    // 在 [min_pos, max_pos] 范围内找最近的句子结束符（。！？.!?）
    static int FindSentenceBoundary(const std::string& text, int min_pos, int max_pos) {
        for (int i = max_pos; i >= min_pos; --i) {
            char c = text[i];
            if (c == '。' || c == '！' || c == '？' || c == '.' || c == '!' || c == '?') {
                return i + 1; // 包含结束符
            }
        }
        return max_pos; // 没找到，用原位置
    }
};

} // namespace mindvault::services
