#pragma once
// Permission routes: /api/notes/:id/permissions
// 权限表在用户数据库中（每用户独立）

#include "../utils/response.h"
#include "user_routes.h"
#include "crow_all.h"
#include <nlohmann/json.hpp>

namespace mindvault::routes {

// 获取用户数据库
inline std::shared_ptr<Database> PermGetUserDb(const crow::request& req) {
    std::string token = req.get_header_value("Authorization");
    if (token.empty() || token.substr(0, 7) != "Bearer ") return nullptr;
    token = token.substr(7);
    auto pos = token.find('_');
    if (pos == std::string::npos) return nullptr;
    try {
        int64_t uid = std::stoll(token.substr(0, pos));
        return UserDbManager::Instance().GetUserDb(uid);
    } catch (...) { return nullptr; }
}

inline void RegisterPermRoutes(crow::App<>& app, Database& db) {
    // GET /api/notes/:id/permissions - 获取笔记权限
    CROW_ROUTE(app, "/api/notes/<int>/permissions").methods("GET"_method)
    ([&db](const crow::request& req, int64_t note_id) {
        auto udb = PermGetUserDb(req);
        if (!udb) return crow::response(401, utils::Error("未登录").dump());
        auto list = udb->Query(
            "SELECT id, note_id, user_id, role, created_at "
            "FROM permissions WHERE note_id = ? ORDER BY created_at", {note_id}
        );
        return crow::response(utils::Success(list).dump());
    });

    // POST /api/notes/:id/permissions - 添加权限
    CROW_ROUTE(app, "/api/notes/<int>/permissions").methods("POST"_method)
    ([&db](const crow::request& req, int64_t note_id) {
        try {
            auto udb = PermGetUserDb(req);
            if (!udb) return crow::response(401, utils::Error("未登录").dump());
            auto body = nlohmann::json::parse(req.body);
            int64_t target_uid = body.value("user_id", 0);
            std::string role = body.value("role", std::string("viewer"));
            if (!target_uid) return crow::response(400, utils::Error("user_id required").dump());
            if (role != "viewer" && role != "editor") return crow::response(400, utils::Error("role must be viewer or editor").dump());
            
            // 验证笔记存在
            auto note = udb->QueryOne("SELECT id FROM notes WHERE id = ? AND is_deleted = 0", {note_id});
            if (note.is_null()) return crow::response(404, utils::Error("笔记不存在").dump());
            
            udb->Execute("INSERT OR REPLACE INTO permissions (note_id, user_id, role) VALUES (?, ?, ?)",
                {note_id, target_uid, role});
            return crow::response(201, utils::Success().dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // DELETE /api/notes/:id/permissions - 删除权限
    CROW_ROUTE(app, "/api/notes/<int>/permissions").methods("DELETE"_method)
    ([&db](const crow::request& req, int64_t note_id) {
        try {
            auto udb = PermGetUserDb(req);
            if (!udb) return crow::response(401, utils::Error("未登录").dump());
            auto body = nlohmann::json::parse(req.body);
            int64_t target_uid = body.value("user_id", 0);
            if (!target_uid) return crow::response(400, utils::Error("user_id required").dump());
            udb->Execute("DELETE FROM permissions WHERE note_id = ? AND user_id = ?", {note_id, target_uid});
            return crow::response(utils::Success().dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });
}

}
