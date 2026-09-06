#pragma once
// 笔记业务逻辑层：CRUD + Wiki 链接解析

#include "../database.h"
#include "../models/note.h"
#include "chunk_service.h"
#include <string>
#include <vector>
#include <regex>
#include <iostream>
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
    nlohmann::json Create(const std::string& title, const std::string& content = "", const std::string& folder = "default",
                              const std::string& api_url = "", const std::string& api_key = "", const std::string& model = "") {
        db_.Execute(
            "INSERT INTO notes (title, content, folder) VALUES (?, ?, ?)",
            {title, content, folder}
        );
        int64_t new_id = db_.LastInsertId();
        
        // 同步 FTS5
        try {
            db_.Execute("INSERT INTO notes_fts(rowid, title, content) VALUES (?, ?, ?)", {new_id, title, content});
        } catch (...) {}
        
        // 更新链接关系
        UpdateLinks(new_id, content);

        // P1-5: 重建切块 + embedding（有 API 配置时同步调，3 秒超时；没配置只存文本）
        try {
            ChunkService chunk_svc(db_);
            chunk_svc.RebuildChunksForNote(new_id, title, content, api_url, api_key, model);
        } catch (...) {
            // chunk 重建失败不影响笔记保存
        }

        return GetById(new_id);
    }

    // 更新笔记
    nlohmann::json Update(int64_t id, const std::string& title, const std::string& content, const std::string& folder = "",
                            const std::string& api_url = "", const std::string& api_key = "", const std::string& model = "") {
        // 先手动同步 FTS5（删除旧记录）
        try {
            auto old = GetById(id);
            if (!old.is_null()) {
                std::string old_title = old.value("title", "");
                std::string old_content = old.value("content", "");
                db_.Execute("DELETE FROM notes_fts WHERE rowid = ?", {id});
            }
        } catch (...) {
            // FTS5 操作失败不影响主表更新
        }
        
        // 更新主表
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
        
        // 同步 FTS5（插入新记录）
        try {
            db_.Execute("INSERT INTO notes_fts(rowid, title, content) VALUES (?, ?, ?)", {id, title, content});
        } catch (...) {
            // FTS5 操作失败不影响主表更新
        }
        
        UpdateLinks(id, content);

        // P1-5: 重建切块 + embedding（有 API 配置时同步调，3 秒超时；没配置只存文本）
        try {
            ChunkService chunk_svc(db_);
            chunk_svc.RebuildChunksForNote(id, title, content, api_url, api_key, model);
        } catch (...) {
            // chunk 重建失败不影响笔记保存
        }

        return GetById(id);
    }

    // 软删除笔记
    bool Delete(int64_t id) {
        // 先清理 FTS5
        try {
            db_.Execute("DELETE FROM notes_fts WHERE rowid = ?", {id});
        } catch (...) {}
        
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
        auto note = db_.QueryOne(
            "SELECT id, title, content, folder FROM notes WHERE id = ? AND is_deleted = 1",
            {id}
        );
        if (note.is_null()) return nullptr;
        
        db_.Execute(
            "UPDATE notes SET is_deleted = 0, updated_at = datetime('now','localtime') WHERE id = ? AND is_deleted = 1",
            {id}
        );
        
        // 重新同步 FTS5
        try {
            db_.Execute("INSERT INTO notes_fts(rowid, title, content) VALUES (?, ?, ?)",
                {id, note["title"].get<std::string>(), note["content"].get<std::string>()});
        } catch (...) {}
        
        return db_.QueryOne(
            "SELECT id, title, content, folder, is_deleted, created_at, updated_at FROM notes WHERE id = ?",
            {id}
        );
    }

    // 永久删除笔记
    bool PermanentDelete(int64_t id) {
        // 先清理 FTS5
        try { db_.Execute("DELETE FROM notes_fts WHERE rowid = ?", {id}); } catch (...) {}
        
        db_.Execute("DELETE FROM notes WHERE id = ? AND is_deleted = 1", {id});
        return db_.Changes() > 0;
    }

    // 清空回收站
    int EmptyTrash() {
        // 先清理 FTS5
        try { db_.Execute("DELETE FROM notes_fts WHERE rowid IN (SELECT id FROM notes WHERE is_deleted = 1)"); } catch (...) {}
        
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

    // ─── P1-7 关联笔记推荐：link_edges 强信号 + 向量弱信号融合 ───
    // 强信号（人工双向链接）+2.0，弱信号（向量余弦相似度）0~1.0
    // 两路都命中的排最前，只有链接的次之，只有向量的再次之
    nlohmann::json GetRecommendations(int64_t note_id, int limit = 10) {
        std::unordered_map<int64_t, double> scores;
        std::unordered_map<int64_t, std::string> titles;
        std::unordered_map<int64_t, std::string> folders;
        std::unordered_map<int64_t, bool> is_linked;

        // ── 强信号路：正向 + 反向链接，去重，每个 +2.0 ──
        auto fwd = GetForwardLinks(note_id);
        auto bwd = GetBacklinks(note_id);
        for (auto& n : fwd) {
            int64_t nid = n.value("id", int64_t(0));
            if (nid > 0 && nid != note_id) {
                scores[nid] += 2.0;
                titles[nid] = n.value("title", std::string(""));
                is_linked[nid] = true;
            }
        }
        for (auto& n : bwd) {
            int64_t nid = n.value("id", int64_t(0));
            if (nid > 0 && nid != note_id) {
                scores[nid] += 2.0;
                titles[nid] = n.value("title", std::string(""));
                is_linked[nid] = true;
            }
        }

        // ── 弱信号路：向量余弦相似度 ──
        try {
            // 拿当前笔记的 chunk embedding
            auto my_chunks = db_.GetChunksByNote(note_id);
            std::vector<std::vector<double>> my_embs;
            for (auto& c : my_chunks) {
                std::string emb_str = c.value("embedding_json", std::string(""));
                if (emb_str.empty()) continue;
                try {
                    auto j = nlohmann::json::parse(emb_str);
                    if (!j.is_array()) continue;
                    std::vector<double> emb;
                    for (auto& v : j) emb.push_back(v.get<double>());
                    if (!emb.empty()) my_embs.push_back(emb);
                } catch (...) {}
            }

            if (!my_embs.empty()) {
                // 拿所有笔记的 chunk（排除当前笔记），算每个笔记的最大相似度
                auto all_chunks = db_.GetAllChunks();
                std::unordered_map<int64_t, double> max_sim;
                for (auto& c : all_chunks) {
                    int64_t nid = c.value("note_id", int64_t(0));
                    if (nid <= 0 || nid == note_id) continue;
                    std::string emb_str = c.value("embedding_json", std::string(""));
                    if (emb_str.empty()) continue;
                    std::vector<double> chunk_emb;
                    try {
                        auto j = nlohmann::json::parse(emb_str);
                        if (!j.is_array()) continue;
                        for (auto& v : j) chunk_emb.push_back(v.get<double>());
                    } catch (...) { continue; }
                    if (chunk_emb.empty()) continue;

                    // 和当前笔记所有 chunk 算余弦，取最大值
                    double best = 0.0;
                    for (auto& my_emb : my_embs) {
                        if (chunk_emb.size() != my_emb.size()) continue;
                        double sim = ChunkService::CosineSimilarity(my_emb, chunk_emb);
                        if (sim > best) best = sim;
                    }
                    if (best > max_sim[nid]) max_sim[nid] = best;
                    titles[nid] = c.value("title", std::string(""));
                    folders[nid] = c.value("folder", std::string(""));
                }

                // 向量相似度加入分数
                for (auto& [nid, sim] : max_sim) {
                    scores[nid] += sim;
                }
            }
        } catch (const std::exception& e) {
            // 向量路失败不影响强信号路结果
            std::cerr << "[Recommend] vector path failed: " << e.what() << std::endl;
        }

        // ── 排序 + 输出 ──
        std::vector<std::pair<double, int64_t>> ranked;
        for (auto& [nid, score] : scores) {
            ranked.push_back({score, nid});
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });

        nlohmann::json results = nlohmann::json::array();
        int count = std::min(limit, (int)ranked.size());
        for (int i = 0; i < count; ++i) {
            int64_t nid = ranked[i].second;
            nlohmann::json item;
            item["id"] = nid;
            item["title"] = titles.count(nid) ? titles[nid] : "";
            item["folder"] = folders.count(nid) ? folders[nid] : "";
            item["score"] = ranked[i].first;
            // source: both=链接+向量, linked=只有链接, similar=只有向量
            bool linked = is_linked.count(nid) && is_linked[nid];
            if (linked && ranked[i].first > 2.0) item["source"] = "both";
            else if (linked) item["source"] = "linked";
            else item["source"] = "similar";
            results.push_back(item);
        }
        return results;
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
        try {
            std::regex re(R"(\[\[([^\]]+)\]\])");
            auto begin = std::sregex_iterator(content.begin(), content.end(), re);
            auto end = std::sregex_iterator();
            for (auto it = begin; it != end; ++it) {
                links.push_back((*it)[1].str());
            }
        } catch (...) {
            // regex 失败时返回空
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
