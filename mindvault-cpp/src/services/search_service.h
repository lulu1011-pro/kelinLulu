#pragma once
// 全文搜索服务：基于 FTS5

#include "../database.h"
#include <string>
#include <nlohmann/json.hpp>

namespace mindvault::services {

class SearchService {
public:
    explicit SearchService(Database& db) : db_(db) {}

    // FTS5 全文搜索，返回匹配笔记列表
    nlohmann::json Search(const std::string& query, int limit = 50) {
        if (query.empty()) return nlohmann::json::array();

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
        )", {query, limit});
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

private:
    Database& db_;
};

} // namespace mindvault::services
