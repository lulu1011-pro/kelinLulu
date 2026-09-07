#pragma once
// 全文搜索服务：基于 FTS5 + 中文友好的多路回退
//
// 为什么要有回退：SQLite FTS5 默认 unicode61 分词器把中文按"连续字符"
// 当 phrase 匹配，用户一句自然语言长 query（如"总结一下C++基础这个笔记"）
// 会被当成一个超长 phrase，几乎必然 0 命中。所以检索策略必须是：
//   第 1 路：标题 LIKE（标题短，命中率高）
//   第 2 路：FTS5 OR 查询（把 query 拆成多个关键词，OR 连接）
//   第 3 路：正文 LIKE（最后兜底）

#include "../database.h"
#include "chunk_service.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace mindvault::services {

class SearchService {
public:
    explicit SearchService(Database& db) : db_(db) {}

    // ─── 中文检索辅助：停用词表 ───
    // 自然语言问句里的虚词/语气词/泛指词，对检索无区分度
    static const std::vector<std::string>& StopWords() {
        static const std::vector<std::string> words = {
            "什么", "怎么", "怎样", "如何", "为什么", "哪些", "哪个", "这个", "那个", "这些", "那些",
            "里面", "中的", "上的", "什么", "一下", "一些", "一个", "一种", "的东西", "东西",
            "笔记", "内容", "文章", "文档", "文件", "里面", "关于", "请问", "总结", "介绍", "讲解",
            "说说", "讲讲", "帮我", "给我", "告诉", "知道", "介绍下", "总结下", "有没有", "有什么",
            "吗", "呢", "啊", "吧", "呀", "哦", "嗯", "的", "了", "是", "在", "有", "和", "与", "或",
            "对", "把", "被", "让", "给", "为", "而", "并", "且", "这", "那", "我", "你", "他", "她",
            "它", "们", "也", "都", "就", "还", "又", "再", "能", "会", "要", "想", "可", "以", "从",
            "到", "向", "跟", "同", "比", "更", "最", "很", "太", "好", "看", "写", "做", "用", "其",
            "时", "候", "后", "前", "先", "间", "分", "别", "之", "以", "及", "个", "种", "条", "些",
        };
        return words;
    }

    // 判断字符是否为中文字符（UTF-8 三字节，E4-E9 开头）
    static bool IsChineseLead(unsigned char c) {
        return c >= 0xE4 && c <= 0xE9;
    }

    // 提取连续中文字符段（跳过非中文），返回 UTF-8 子串列表
    static std::vector<std::string> ExtractChineseSegments(const std::string& query) {
        std::vector<std::string> segs;
        size_t i = 0, n = query.size();
        while (i < n) {
            // 一个汉字占 3 字节，需保证 i+3 <= n
            if (IsChineseLead((unsigned char)query[i]) && i + 3 <= n) {
                size_t start = i;
                i += 3;
                while (i + 3 <= n && IsChineseLead((unsigned char)query[i])) {
                    i += 3;
                }
                segs.push_back(query.substr(start, i - start));
            } else {
                ++i;
            }
        }
        return segs;
    }

    // 从中文段提取关键词：优先 2 字词（滑窗），过滤停用词
    static void AddChineseKeywords(const std::string& seg, std::vector<std::string>& out) {
        // 滑窗取 2 字词；若剩余不足 2 字则整段
        std::vector<std::string> grams;
        for (size_t i = 0; i + 6 <= seg.size(); i += 3) {
            grams.push_back(seg.substr(i, 6));  // 2 汉字
        }
        if (grams.empty() && !seg.empty()) grams.push_back(seg);

        for (auto& g : grams) {
            bool stop = false;
            for (auto& sw : StopWords()) {
                if (g.find(sw) != std::string::npos) { stop = true; break; }
            }
            if (!stop) out.push_back(g);
        }
    }

    // 从 query 提取英文/数字关键词（C++、STL、shared_ptr、11 等）
    static void AddAsciiKeywords(const std::string& query, std::vector<std::string>& out) {
        std::string cur;
        for (char c : query) {
            if (std::isalnum((unsigned char)c) || c == '_') {
                cur += c;
            } else {
                if (!cur.empty()) {
                    if (cur.size() >= 2) out.push_back(cur);  // 单字母太宽泛，跳过
                    cur.clear();
                }
            }
        }
        if (!cur.empty() && cur.size() >= 2) out.push_back(cur);
    }

    // 提取检索关键词列表（中文 2 字词 + 英文/数字 token），去重保序
    static std::vector<std::string> ExtractKeywords(const std::string& query) {
        std::vector<std::string> kws;
        auto cjk = ExtractChineseSegments(query);
        for (auto& seg : cjk) AddChineseKeywords(seg, kws);
        AddAsciiKeywords(query, kws);

        // 去重（保序）
        std::vector<std::string> uniq;
        for (auto& k : kws) {
            if (std::find(uniq.begin(), uniq.end(), k) == uniq.end()) uniq.push_back(k);
        }
        return uniq;
    }

    // 按笔记 id 列表查询完整信息（供 RAG 取正文）
    // ids: 逗号分隔字符串
    nlohmann::json QueryByNoteIds(const std::string& idListCsv, int contentLimit = 1500) {
        if (idListCsv.empty()) return nlohmann::json::array();
        // 校验只含数字与逗号，防注入
        for (char c : idListCsv) {
            if (!std::isdigit((unsigned char)c) && c != ',') return nlohmann::json::array();
        }
        return db_.Query(R"(
            SELECT id, title, folder, updated_at,
                   title as title_highlight,
                   substr(content, 1, ?) as content_highlight
            FROM notes
            WHERE id IN (SELECT value FROM json_each(?)) AND is_deleted = 0
            ORDER BY updated_at DESC
            LIMIT 50
        )", {contentLimit, "[" + idListCsv + "]"});
    }

    // 标题模糊搜索（LIKE，最高优先级路）
    nlohmann::json SearchByTitleKeywords(const std::vector<std::string>& kws, int limit = 20) {
        if (kws.empty()) return nlohmann::json::array();
        // 标题只要命中任一关键词即可
        std::vector<nlohmann::json> params;
        std::string sql =
            "SELECT id, title, folder, updated_at, title as title_highlight, "
            "substr(content, 1, 1500) as content_highlight "
            "FROM notes WHERE is_deleted = 0 AND (";
        for (size_t i = 0; i < kws.size(); ++i) {
            if (i > 0) sql += " OR ";
            sql += "title LIKE ?";
            params.push_back("%" + kws[i] + "%");
        }
        sql += ") ORDER BY updated_at DESC LIMIT ?";
        params.push_back(limit);
        return db_.Query(sql, params);
    }

    // FTS5 全文搜索，返回匹配笔记列表
    nlohmann::json Search(const std::string& query, int limit = 50) {
        if (query.empty()) return nlohmann::json::array();

        // 第 0 路：直接标题精确/模糊搜索——自然语言里的标题命中
        auto kws = ExtractKeywords(query);

        // 第 1 路：标题 LIKE（关键词任一命中标题）
        if (!kws.empty()) {
            auto by_title = SearchByTitleKeywords(kws, limit);
            if (!by_title.empty()) return by_title;
        }

        // 第 2 路：FTS5 MATCH——把关键词 OR 起来（短语引号包裹，避免特殊字符语法错误）
        if (!kws.empty()) {
            std::string matchExpr;
            for (size_t i = 0; i < kws.size() && i < 8; ++i) {
                if (i > 0) matchExpr += " OR ";
                matchExpr += "\"" + kws[i] + "\"";
            }
            if (!matchExpr.empty()) {
                try {
                    nlohmann::json fts_results = db_.Query(R"(
                        SELECT n.id, n.title, n.folder, n.updated_at,
                               snippet(notes_fts, 0, '>>>', '<<<', '...', 20) as title_highlight,
                               snippet(notes_fts, 1, '>>>', '<<<', '...', 60) as content_highlight
                        FROM notes_fts f
                        INNER JOIN notes n ON n.id = f.rowid
                        WHERE notes_fts MATCH ? AND n.is_deleted = 0
                        ORDER BY rank
                        LIMIT ?
                    )", {matchExpr, limit});
                    if (!fts_results.empty()) return fts_results;
                } catch (...) { /* MATCH 语法错误则落到正文 LIKE */ }
            }
        }

        // 第 3 路：正文 LIKE 兜底（取第一个有效关键词）
        for (auto& k : kws) {
            if (k.size() < 2) continue;
            std::string likePattern = "%" + k + "%";
            auto results = db_.Query(R"(
                SELECT id, title, folder, updated_at,
                       title as title_highlight,
                       substr(content, 1, 1500) as content_highlight
                FROM notes
                WHERE (title LIKE ? OR content LIKE ?) AND is_deleted = 0
                ORDER BY updated_at DESC
                LIMIT ?
            )", {likePattern, likePattern, limit});
            if (!results.empty()) return results;
        }

        // 最后一搏：原样整句 LIKE（最宽松）
        return SearchByRawQuery(query, limit);
    }

    // 原样整句子串 LIKE（最宽松兜底，仅当以上全空时）
    nlohmann::json SearchByRawQuery(const std::string& query, int limit = 50) {
        std::string likePattern = "%" + query + "%";
        return db_.Query(R"(
            SELECT id, title, folder, updated_at,
                   title as title_highlight,
                   substr(content, 1, 1500) as content_highlight
            FROM notes
            WHERE (title LIKE ? OR content LIKE ?) AND is_deleted = 0
            ORDER BY updated_at DESC
            LIMIT ?
        )", {likePattern, likePattern, limit});
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

    // 从 chat 接口地址推导 embeddings 接口地址：
    //   https://open.bigmodel.cn/api/paas/v4/chat/completions
    //     → https://open.bigmodel.cn/api/paas/v4/embeddings
    // 各家 OpenAI 兼容网关都遵循 "路径/chat/completions → /embeddings" 的规律
    // （智谱、阿里百炼 compatible-mode、火山方舟 ark、OpenAI 本体均是如此）
    static std::string DeriveEmbeddingsUrl(const std::string& chat_url) {
        const std::string suffix = "/chat/completions";
        if (chat_url.size() > suffix.size() &&
            chat_url.compare(chat_url.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return chat_url.substr(0, chat_url.size() - suffix.size()) + "/embeddings";
        }
        const std::string emb_suffix = "/embeddings";
        if (chat_url.size() >= emb_suffix.size() &&
            chat_url.compare(chat_url.size() - emb_suffix.size(), emb_suffix.size(), emb_suffix) == 0) {
            return chat_url; // 已是 embeddings 地址
        }
        return ""; // 无法推导则放弃向量路（关键词检索不受影响）
    }

    // ─── P1-5 混合检索：FTS5 关键词路 + 向量语义路 + RRF 融合 ───
    // api_url/api_key = 对话模型的接口与密钥；embedding_model 为空时跳过向量路（降级纯 FTS5）
    // embedding 接口地址由 api_url 自动推导（DeriveEmbeddingsUrl）
    nlohmann::json HybridSearch(const std::string& query, int limit = 50,
                                  const std::string& api_url = "", const std::string& api_key = "",
                                  const std::string& model = "",
                                  const std::string& embedding_model = "") {
        if (query.empty()) return nlohmann::json::array();

        // 第 1 步：FTS5 关键词路（取 limit*2，给融合留余量）
        nlohmann::json fts_results = Search(query, limit * 2);

        // 第 2 步：向量语义路（需要：已配置 embedding 模型 + 能从对话地址推导出向量地址）
        nlohmann::json vec_results = nlohmann::json::array();
        bool vector_ok = false;
        std::string embedding_url = DeriveEmbeddingsUrl(api_url);
        if (!api_key.empty() && !embedding_model.empty() && !embedding_url.empty()) {
            std::cout << "[HybridSearch] embedding: url=" << embedding_url << ", model=" << embedding_model << std::endl;
            auto query_emb = ChunkService::EmbedText(embedding_url, api_key, embedding_model, query);
            if (!query_emb.empty()) {
                ChunkService chunk_svc(db_);
                vec_results = chunk_svc.VectorSearch(query_emb, limit * 2);
                vector_ok = !vec_results.empty();
            } else {
                std::cout << "[HybridSearch] embedding failed, fallback to FTS5 only" << std::endl;
            }
        } else if (!embedding_model.empty() && embedding_url.empty()) {
            std::cout << "[HybridSearch] cannot derive embeddings url from api_url: " << api_url << std::endl;
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
