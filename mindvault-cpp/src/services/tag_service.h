#pragma once
// 标签管理服务

#include "../database.h"
#include "../models/tag.h"
#include <nlohmann/json.hpp>

namespace mindvault::services {

class TagService {
public:
    explicit TagService(Database& db) : db_(db) {}

    // 获取所有标签
    nlohmann::json List() {
        return db_.Query(R"(
            SELECT t.id, t.name, COUNT(nt.note_id) as note_count
            FROM tags t
            LEFT JOIN note_tags nt ON nt.tag_id = t.id
            LEFT JOIN notes n ON n.id = nt.note_id AND n.is_deleted = 0
            GROUP BY t.id
            ORDER BY t.name
        )");
    }

    // 给笔记添加标签（自动创建不存在的标签）
    bool AddTagToNote(int64_t note_id, const std::string& tag_name) {
        // 确保标签存在
        db_.Execute("INSERT OR IGNORE INTO tags (name) VALUES (?)", {tag_name});
        auto tag = db_.QueryOne("SELECT id FROM tags WHERE name = ?", {tag_name});
        if (tag.is_null()) return false;

        int64_t tag_id = tag["id"].get<int64_t>();
        db_.Execute("INSERT OR IGNORE INTO note_tags (note_id, tag_id) VALUES (?, ?)", {note_id, tag_id});
        return true;
    }

    // 移除笔记标签
    bool RemoveTagFromNote(int64_t note_id, const std::string& tag_name) {
        auto tag = db_.QueryOne("SELECT id FROM tags WHERE name = ?", {tag_name});
        if (tag.is_null()) return false;
        db_.Execute("DELETE FROM note_tags WHERE note_id = ? AND tag_id = ?", {note_id, tag["id"].get<int64_t>()});
        return db_.Changes() > 0;
    }

    // 获取笔记的标签
    nlohmann::json GetNoteTags(int64_t note_id) {
        return db_.Query(R"(
            SELECT t.id, t.name
            FROM tags t
            INNER JOIN note_tags nt ON nt.tag_id = t.id
            WHERE nt.note_id = ?
            ORDER BY t.name
        )", {note_id});
    }

    // 按标签名获取笔记列表
    nlohmann::json GetNotesByTag(const std::string& tag_name) {
        return db_.Query(R"(
            SELECT n.id, n.title, n.folder, n.is_deleted, n.created_at, n.updated_at
            FROM notes n
            INNER JOIN note_tags nt ON nt.note_id = n.id
            INNER JOIN tags t ON t.id = nt.tag_id
            WHERE t.name = ? AND n.is_deleted = 0
            ORDER BY n.updated_at DESC
        )", {tag_name});
    }

private:
    Database& db_;
};

} // namespace mindvault::services
