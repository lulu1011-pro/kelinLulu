#pragma once
// Share routes: /api/share/*

#include "../database.h"
#include "../utils/response.h"
#include "user_routes.h"
#include "crow_all.h"
#include <nlohmann/json.hpp>
#include <string>
#include <random>
#include <chrono>

namespace mindvault::routes {

// 生成随机短码
inline std::string GenerateShareCode(int length = 8) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);
    std::string result;
    for (int i = 0; i < length; ++i) result += chars[dist(gen)];
    return result;
}

// 从 token 获取用户 ID
inline int64_t GetUserIdFromToken3(const crow::request& req) {
    std::string token = req.get_header_value("Authorization");
    if (token.empty() || token.substr(0, 7) != "Bearer ") return 0;
    token = token.substr(7);
    auto pos = token.find('_');
    if (pos == std::string::npos) return 0;
    try {
        return std::stoll(token.substr(0, pos));
    } catch (...) {
        return 0;
    }
}

inline std::shared_ptr<Database> GetUserDb3(const crow::request& req) {
    int64_t user_id = GetUserIdFromToken3(req);
    if (user_id == 0) return nullptr;
    return UserDbManager::Instance().GetUserDb(user_id);
}

inline void RegisterShareRoutes(crow::App<>& app, Database& db) {
    // 初始化分享表（在主数据库中）
    db.Execute(R"(
        CREATE TABLE IF NOT EXISTS share_links (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id     INTEGER NOT NULL,
            note_id     INTEGER NOT NULL,
            share_code  TEXT NOT NULL UNIQUE,
            permission  TEXT NOT NULL DEFAULT 'view',
            created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime')),
            expires_at  TEXT
        )
    )");

    // POST /api/share - 创建分享链接
    CROW_ROUTE(app, "/api/share").methods("POST"_method)
    ([&db](const crow::request& req) {
        try {
            int64_t user_id = GetUserIdFromToken3(req);
            if (user_id == 0) {
                return crow::response(401, utils::Error("未登录").dump());
            }

            auto body = nlohmann::json::parse(req.body);
            int64_t note_id = body.value("note_id", 0);
            std::string permission = body.value("permission", std::string("view"));
            int expires_hours = body.value("expires_hours", 0);

            if (note_id == 0) {
                return crow::response(400, utils::Error("note_id is required").dump());
            }

            // 获取笔记信息
            auto user_db = UserDbManager::Instance().GetUserDb(user_id);
            auto note = user_db->QueryOne("SELECT id, title FROM notes WHERE id = ? AND is_deleted = 0", {note_id});
            if (note.is_null()) {
                return crow::response(404, utils::Error("Note not found").dump());
            }

            // 生成分享码
            std::string share_code = GenerateShareCode();

            // 计算过期时间
            std::string expires_at = "";
            if (expires_hours > 0) {
                // 简化：存储小时数，实际应用应计算具体时间
                expires_at = std::to_string(expires_hours);
            }

            db.Execute(
                "INSERT INTO share_links (user_id, note_id, share_code, permission, expires_at) VALUES (?, ?, ?, ?, ?)",
                {user_id, note_id, share_code, permission, expires_at}
            );

            nlohmann::json result = {
                {"share_code", share_code},
                {"note_id", note_id},
                {"title", note["title"]},
                {"permission", permission},
                {"url", "/share/" + share_code}
            };
            return crow::response(201, utils::Success(result).dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // GET /api/share/list - 获取用户的分享列表（必须放在 :code 前面）
    CROW_ROUTE(app, "/api/share/list").methods("GET"_method)
    ([&db](const crow::request& req) {
        int64_t user_id = GetUserIdFromToken3(req);
        if (user_id == 0) {
            return crow::response(401, utils::Error("未登录").dump());
        }
        auto shares = db.Query(
            "SELECT id, note_id, share_code, permission, created_at FROM share_links WHERE user_id = ? ORDER BY created_at DESC",
            {user_id}
        );
        return crow::response(utils::Success(shares).dump());
    });

    // GET /api/share/:code - 获取分享的笔记（无需登录）
    CROW_ROUTE(app, "/api/share/<str>").methods("GET"_method)
    ([&db](const std::string& share_code) {
        try {
            auto share = db.QueryOne(
                "SELECT user_id, note_id, permission, expires_at FROM share_links WHERE share_code = ?",
                {share_code}
            );
            if (share.is_null()) {
                return crow::response(404, utils::Error("分享链接不存在或已过期").dump());
            }

            int64_t user_id = share["user_id"].get<int64_t>();
            int64_t note_id = share["note_id"].get<int64_t>();

            // 获取用户数据库
            auto user_db = UserDbManager::Instance().GetUserDb(user_id);
            auto note = user_db->QueryOne(
                "SELECT id, title, content, folder, created_at, updated_at FROM notes WHERE id = ? AND is_deleted = 0",
                {note_id}
            );
            if (note.is_null()) {
                return crow::response(404, utils::Error("笔记不存在").dump());
            }

            // 获取用户信息
            auto user = db.QueryOne("SELECT username, nickname FROM users WHERE id = ?", {user_id});

            nlohmann::json result = {
                {"note", note},
                {"author", user.value("nickname", user.value("username", ""))},
                {"permission", share["permission"]}
            };
            return utils::JsonResp(utils::Success(result));
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // POST /api/share/:code/import - 导入共享笔记到用户数据库
    CROW_ROUTE(app, "/api/share/<str>/import").methods("POST"_method)
    ([&db](const crow::request& req, const std::string& share_code) {
        try {
            int64_t user_id = GetUserIdFromToken3(req);
            if (user_id == 0) {
                return crow::response(401, utils::Error("未登录").dump());
            }

            // 获取分享信息
            auto share = db.QueryOne(
                "SELECT user_id, note_id FROM share_links WHERE share_code = ?", {share_code}
            );
            if (share.is_null()) {
                return crow::response(404, utils::Error("分享链接不存在").dump());
            }

            int64_t owner_id = share["user_id"].get<int64_t>();
            int64_t note_id = share["note_id"].get<int64_t>();

            // 获取笔记内容
            auto owner_db = UserDbManager::Instance().GetUserDb(owner_id);
            auto note = owner_db->QueryOne(
                "SELECT title, content, folder FROM notes WHERE id = ? AND is_deleted = 0", {note_id}
            );
            if (note.is_null()) {
                return crow::response(404, utils::Error("笔记不存在").dump());
            }

            // 复制到当前用户数据库
            auto user_db = UserDbManager::Instance().GetUserDb(user_id);
            user_db->Execute(
                "INSERT INTO notes (title, content, folder) VALUES (?, ?, ?)",
                {note["title"].get<std::string>(),
                 note["content"].get<std::string>(),
                 note["folder"].get<std::string>()}
            );
            int64_t new_id = user_db->LastInsertId();

            // 同步 FTS5
            try {
                user_db->Execute("INSERT INTO notes_fts(rowid, title, content) VALUES (?, ?, ?)",
                    {new_id, note["title"].get<std::string>(), note["content"].get<std::string>()});
            } catch (...) {}

            auto result = user_db->QueryOne(
                "SELECT id, title, folder, created_at, updated_at FROM notes WHERE id = ?", {new_id}
            );
            return crow::response(201, utils::Success(result).dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // DELETE /api/share/:id - 删除分享链接
    CROW_ROUTE(app, "/api/share/<int>").methods("DELETE"_method)
    ([&db](const crow::request& req, int64_t id) {
        int64_t user_id = GetUserIdFromToken3(req);
        if (user_id == 0) {
            return crow::response(401, utils::Error("未登录").dump());
        }

        db.Execute("DELETE FROM share_links WHERE id = ? AND user_id = ?", {id, user_id});
        return crow::response(utils::Success().dump());
    });
}

}
