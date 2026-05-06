#pragma once
// 笔记业务逻辑层：CRUD + Wiki 链接解析

#include "../database.h"
#include "../models/note.h"
#include <string>
#include <vector>
#include <regex>
#include <nlohmann/json.hpp>

namespace mindvault::services {

class NoteService {
public:
    explicit NoteService(Database& db) : db_(db) {}

    // ─── CRUD ───

    // 获取笔记列表（不含 content，支持 folder 过滤）
    nlohmann::json List(const std::string& folder = "") {
        std::string sql = R"(
            SELECT id, title, folder, is_deleted, sort_order, created_at, updated_at
            FROM notes WHERE is_deleted = 0
        )";
        std::vector<nlohmann::json> params;
        if (!folder.empty()) {
            sql += " AND folder = ?";
            params.push_back(folder);
        }
        sql += " ORDER BY sort_order ASC, updated_at DESC";
        return db_.Query(sql, params);
    }

    // 更新笔记排序
    bool UpdateSortOrder(int64_t id, int sort_order) {
        db_.Execute(
            "UPDATE notes SET sort_order = ? WHERE id = ? AND is_deleted = 0",
            {sort_order, id}
        );
        return db_.Changes() > 0;
    }

    // 批量更新排序
    void BatchUpdateSortOrder(const std::vector<std::pair<int64_t, int>>& orders) {
        db_.Begin();
        for (const auto& [id, order] : orders) {
            db_.Execute(
                "UPDATE notes SET sort_order = ? WHERE id = ? AND is_deleted = 0",
                {order, id}
            );
        }
        db_.Commit();
    }

    // 获取笔记详情（含 content）
    nlohmann::json GetById(int64_t id) {
        auto row = db_.QueryOne(
            "SELECT id, title, content, folder, is_deleted, created_at, updated_at FROM notes WHERE id = ? AND is_deleted = 0",
            {id}
        );
        if (row.is_null()) return nullptr;
        return row;
    }

    // 创建笔记
    nlohmann::json Create(const std::string& title, const std::string& content = "", const std::string& folder = "default") {
        db_.Execute(
            "INSERT INTO notes (title, content, folder) VALUES (?, ?, ?)",
            {title, content, folder}
        );
        int64_t new_id = db_.LastInsertId();
        // 更新链接关系
        UpdateLinks(new_id, content);
        return GetById(new_id);
    }

    // 更新笔记
    nlohmann::json Update(int64_t id, const std::string& title, const std::string& content, const std::string& folder = "") {
        // 保存版本历史
        auto current = GetById(id);
        if (!current.is_null()) {
            SaveVersion(id, current["title"].get<std::string>(), current["content"].get<std::string>());
        }

        if (!folder.empty()) {
            db_.Execute(
                "UPDATE notes SET title = ?, content = ?, folder = ?, updated_at = datetime('now','localtime') WHERE id = ? AND is_deleted = 0",
                {title, content, folder, id}
            );
        } else {
            db_.Execute(
                "UPDATE notes SET title = ?, content = ?, updated_at = datetime('now','localtime') WHERE id = ? AND is_deleted = 0",
                {title, content, id}
            );
        }
        // 更新链接关系
        UpdateLinks(id, content);
        return GetById(id);
    }

    // 软删除笔记
    bool Delete(int64_t id) {
        db_.Execute(
            "UPDATE notes SET is_deleted = 1, updated_at = datetime('now','localtime') WHERE id = ?",
            {id}
        );
        return db_.Changes() > 0;
    }

    // 获取回收站笔记列表
    nlohmann::json ListTrash() {
        return db_.Query(R"(
            SELECT id, title, folder, is_deleted, created_at, updated_at
            FROM notes WHERE is_deleted = 1
            ORDER BY updated_at DESC
        )");
    }

    // 恢复笔记
    nlohmann::json Restore(int64_t id) {
        db_.Execute(
            "UPDATE notes SET is_deleted = 0, updated_at = datetime('now','localtime') WHERE id = ? AND is_deleted = 1",
            {id}
        );
        if (db_.Changes() == 0) return nullptr;
        return db_.QueryOne(
            "SELECT id, title, content, folder, is_deleted, created_at, updated_at FROM notes WHERE id = ?",
            {id}
        );
    }

    // 永久删除笔记
    bool PermanentDelete(int64_t id) {
        db_.Execute("DELETE FROM notes WHERE id = ? AND is_deleted = 1", {id});
        return db_.Changes() > 0;
    }

    // 清空回收站
    int EmptyTrash() {
        db_.Execute("DELETE FROM notes WHERE is_deleted = 1");
        return db_.Changes();
    }

    // ─── Wiki 链接 ───

    // 获取反向链接（谁链接到当前笔记）
    nlohmann::json GetBacklinks(int64_t note_id) {
        return db_.Query(R"(
            SELECT n.id, n.title, n.updated_at
            FROM notes n
            INNER JOIN link_edges le ON le.source_id = n.id
            WHERE le.target_id = ? AND n.is_deleted = 0
            ORDER BY n.updated_at DESC
        )", {note_id});
    }

    // 获取正向链接（当前笔记链接到谁）
    nlohmann::json GetForwardLinks(int64_t note_id) {
        return db_.Query(R"(
            SELECT n.id, n.title, n.updated_at
            FROM notes n
            INNER JOIN link_edges le ON le.target_id = n.id
            WHERE le.source_id = ? AND n.is_deleted = 0
            ORDER BY n.updated_at DESC
        )", {note_id});
    }

    // ─── 版本历史 ───

    // 保存版本
    void SaveVersion(int64_t note_id, const std::string& title, const std::string& content) {
        db_.Execute(
            "INSERT INTO versions (note_id, title, content) VALUES (?, ?, ?)",
            {note_id, title, content}
        );
    }

    // 获取版本列表
    nlohmann::json GetVersions(int64_t note_id) {
        return db_.Query(R"(
            SELECT id, note_id, title, created_at
            FROM versions
            WHERE note_id = ?
            ORDER BY created_at DESC
            LIMIT 50
        )", {note_id});
    }

    // 获取版本详情
    nlohmann::json GetVersion(int64_t version_id) {
        return db_.QueryOne(R"(
            SELECT id, note_id, title, content, created_at
            FROM versions
            WHERE id = ?
        )", {version_id});
    }

    // 删除版本
    void DeleteVersions(int64_t note_id) {
        db_.Execute("DELETE FROM versions WHERE note_id = ?", {note_id});
    }

private:
    Database& db_;

    // 从内容中提取 [[标题]] 格式的 Wiki 链接
    std::vector<std::string> ExtractWikiLinks(const std::string& content) {
        std::vector<std::string> links;
        std::regex re(R"(\[\[([^\]]+)\]\])");
        auto begin = std::sregex_iterator(content.begin(), content.end(), re);
        auto end = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            links.push_back((*it)[1].str());
        }
        return links;
    }

    // 增量更新笔记的链接关系
    void UpdateLinks(int64_t note_id, const std::string& content) {
        // 先删除旧的出边
        db_.Execute("DELETE FROM link_edges WHERE source_id = ?", {note_id});

        // 提取新的 Wiki 链接
        auto link_titles = ExtractWikiLinks(content);
        for (const auto& title : link_titles) {
            // 查找目标笔记
            auto target = db_.QueryOne(
                "SELECT id FROM notes WHERE title = ? AND is_deleted = 0 LIMIT 1",
                {title}
            );
            if (!target.is_null()) {
                int64_t target_id = target["id"].get<int64_t>();
                if (target_id != note_id) { // 不链接自己
                    db_.Execute(
                        "INSERT OR IGNORE INTO link_edges (source_id, target_id) VALUES (?, ?)",
                        {note_id, target_id}
                    );
                }
            }
        }
    }
};

} // namespace mindvault::services
