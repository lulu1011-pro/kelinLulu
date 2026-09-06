#pragma once
// 全文搜索服务：基于 FTS5

#include "../database.h"
#include "chunk_service.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace mindvault::services {

class SearchService {
public:
    explicit SearchService(Database& db) : db_(db) {}

    // 转义 FTS5 特殊字符
    static std::string EscapeFTS5(const std::string& query) {
        std::string result;
        for (char c : query) {
            // 只保留字母、数字、中文和空格
            if (std::isalnum(c) || c == ' ' || c == '\t' || c == '\n' ||
                (c & 0x80)) {  // UTF-8 多字节字符
                result += c;
            }
            // 其他字符替换为空格
            else if (result.empty() || result.back() != ' ') {
                result += ' ';
            }
        }
        // 去除首尾空格
        size_t start = result.find_first_not_of(' ');
        size_t end = result.find_last_not_of(' ');
        if (start == std::string::npos) return "";
        return result.substr(start, end - start + 1);
    }

    // FTS5 全文搜索，返回匹配笔记列表
    nlohmann::json Search(const std::string& query, int limit = 50) {
        if (query.empty()) return nlohmann::json::array();

        // 转义特殊字符
        std::string escaped_query = EscapeFTS5(query);

        // FTS5 MATCH 语法：默认按 OR 匹配关键词
        return db_.Query(R"(
            SELECT n.id, n.title, n.folder, n.updated_at,
                   snippet(notes_fts, 0, '>>>', '<<<', '...', 20) as title_highlight,
                   snippet(notes_fts, 1, '>>>', '<<<', '...', 40) as content_highlight
            FROM notes_fts f
            INNER JOIN notes n ON n.id = f.rowid
            WHERE notes_fts MATCH ? AND n.is_deleted = 0
            ORDER BY rank
            LIMIT ?
        )", {escaped_query, limit});
    }

    // 按标题模糊搜索（非全文索引，走 LIKE）
    nlohmann::json SearchByTitle(const std::string& keyword, int limit = 20) {
        std::string pattern = "%" + keyword + "%";
        return db_.Query(R"(
            SELECT id, title, folder, updated_at
            FROM notes
            WHERE title LIKE ? AND is_deleted = 0
            ORDER BY updated_at DESC
            LIMIT ?
        )", {pattern, limit});
    }

    // ─── P1-5 混合检索：FTS5 关键词路 + 向量语义路 + RRF 融合 ───
    // api_url/api_key/model 为空时跳过向量路，纯 FTS5（降级）
    nlohmann::json HybridSearch(const std::string& query, int limit = 50,
                                  const std::string& api_url = "", const std::string& api_key = "",
                                  const std::string& model = "") {
        if (query.empty()) return nlohmann::json::array();

        // 第 1 步：FTS5 关键词路（取 limit*2，给融合留余量）
        nlohmann::json fts_results = Search(query, limit * 2);

        // 第 2 步：向量语义路
        nlohmann::json vec_results = nlohmann::json::array();
        bool vector_ok = false;
        if (!api_url.empty() && !api_key.empty()) {
            auto query_emb = ChunkService::EmbedText(api_url, api_key, model, query);
            if (!query_emb.empty()) {
                ChunkService chunk_svc(db_);
                vec_results = chunk_svc.VectorSearch(query_emb, limit * 2);
                vector_ok = !vec_results.empty();
            } else {
                std::cout << "[HybridSearch] embedding failed, fallback to FTS5 only" << std::endl;
            }
        }

        // 向量路失败或未配置：直接返回 FTS5 结果
        if (!vector_ok) {
            return fts_results;
        }

        // 第 3 步：RRF 融合（k=60，只看排名不看分数，消除两路量纲差异）
        constexpr int RRF_K = 60;
        struct MergeItem {
            double score = 0.0;
            nlohmann::json fts_item;   // 可能为 null
            nlohmann::json vec_item;   // 可能为 null
            bool has_fts = false;
            bool has_vec = false;
        };
        std::unordered_map<int64_t, MergeItem> merged;

        // FTS5 路：rank 从 1 开始
        for (size_t i = 0; i < fts_results.size(); ++i) {
            int64_t note_id = fts_results[i].value("id", int64_t(0));
            if (note_id == 0) continue;
            auto& item = merged[note_id];
            item.score += 1.0 / (RRF_K + (int)i + 1);
            item.fts_item = fts_results[i];
            item.has_fts = true;
        }

        // 向量路：rank 从 1 开始
        for (size_t i = 0; i < vec_results.size(); ++i) {
            int64_t note_id = vec_results[i].value("note_id", int64_t(0));
            if (note_id == 0) continue;
            auto& item = merged[note_id];
            item.score += 1.0 / (RRF_K + (int)i + 1);
            item.vec_item = vec_results[i];
            item.has_vec = true;
        }

        // 按融合分降序排序
        std::vector<std::pair<double, int64_t>> ranked;
        for (auto& [nid, item] : merged) {
            ranked.push_back({item.score, nid});
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });

        // 第 4 步：合并详情，输出格式与 Search 一致
        nlohmann::json results = nlohmann::json::array();
        int count = std::min(limit, (int)ranked.size());
        for (int i = 0; i < count; ++i) {
            int64_t nid = ranked[i].second;
            auto& item = merged[nid];
            nlohmann::json out;

            if (item.has_fts) {
                // 优先用 FTS5 的完整字段（含 highlight）
                out = item.fts_item;
            } else {
                // 只有向量路：用 chunk_text 做 content_highlight
                out["id"] = nid;
                out["title"] = item.vec_item.value("title", std::string(""));
                out["folder"] = item.vec_item.value("folder", std::string(""));
                out["updated_at"] = "";
                out["title_highlight"] = item.vec_item.value("title", std::string(""));
                std::string chunk_text = item.vec_item.value("chunk_text", std::string(""));
                // 截断过长的 chunk_text
                if (chunk_text.size() > 200) chunk_text = chunk_text.substr(0, 200) + "...";
                out["content_highlight"] = chunk_text;
            }
            results.push_back(out);
        }

        return results;
    }

private:
    Database& db_;
};

} // namespace mindvault::services
