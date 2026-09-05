#pragma once
// 全文搜索服务：基于 FTS5

#include "../database.h"
#include <string>
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

private:
    Database& db_;
};

} // namespace mindvault::services
