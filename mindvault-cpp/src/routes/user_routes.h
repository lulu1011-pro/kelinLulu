#pragma once
// User routes: /api/auth/*

#include "../database.h"
#include "../utils/response.h"
#include "crow_all.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <chrono>
#include <functional>
#include <filesystem>
#include <unordered_map>
#include <mutex>

namespace mindvault::routes {

// 用户数据库管理器
class UserDbManager {
public:
    static UserDbManager& Instance() {
        static UserDbManager instance;
        return instance;
    }

    // 获取用户数据库（不存在则创建）
    std::shared_ptr<Database> GetUserDb(int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = dbs_.find(user_id);
        if (it != dbs_.end()) {
            return it->second;
        }

        // 创建用户独立数据库
        std::string db_path = "data/user_" + std::to_string(user_id) + ".db";
        auto user_db = std::make_shared<Database>(db_path);
        user_db->Init();
        dbs_[user_id] = user_db;

        std::cout << "[UserDB] Created database for user " << user_id << ": " << db_path << std::endl;
        return user_db;
    }

    // 关闭用户数据库
    void CloseUserDb(int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        dbs_.erase(user_id);
    }

private:
    std::unordered_map<int64_t, std::shared_ptr<Database>> dbs_;
    std::mutex mutex_;
};

inline void RegisterUserRoutes(crow::App<>& app, Database& db) {
    // 初始化用户表（在主数据库中）
    db.Execute(R"(
        CREATE TABLE IF NOT EXISTS users (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            username    TEXT NOT NULL UNIQUE,
            password    TEXT NOT NULL,
            nickname    TEXT DEFAULT '',
            created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime')),
            updated_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
        )
    )");

    // POST /api/auth/register - 注册
    CROW_ROUTE(app, "/api/auth/register").methods("POST"_method)
    ([&db](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string username = body.value("username", std::string(""));
            std::string password = body.value("password", std::string(""));
            std::string nickname = body.value("nickname", std::string(""));

            if (username.empty() || password.empty()) {
                return crow::response(400, utils::Error("用户名和密码不能为空").dump());
            }
            if (username.length() < 3) {
                return crow::response(400, utils::Error("用户名至少3个字符").dump());
            }
            if (password.length() < 6) {
                return crow::response(400, utils::Error("密码至少6个字符").dump());
            }

            // 检查用户名是否已存在
            auto existing = db.QueryOne("SELECT id FROM users WHERE username = ?", {username});
            if (!existing.is_null()) {
                return crow::response(400, utils::Error("用户名已存在").dump());
            }

            // 简单密码哈希
            std::string salted = password + "mindvault_salt_2026";
            std::hash<std::string> hasher;
            size_t hash = hasher(salted);
            std::string hashed = std::to_string(hash);

            db.Execute(
                "INSERT INTO users (username, password, nickname) VALUES (?, ?, ?)",
                {username, hashed, nickname.empty() ? username : nickname}
            );

            int64_t user_id = db.LastInsertId();

            // 创建用户独立数据库
            UserDbManager::Instance().GetUserDb(user_id);

            auto user = db.QueryOne("SELECT id, username, nickname, created_at FROM users WHERE id = ?", {user_id});

            // 生成 token
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            std::string token = std::to_string(user_id) + "_" + std::to_string(timestamp) + "_mindvault";

            nlohmann::json result = {
                {"id", user["id"]},
                {"username", user["username"]},
                {"nickname", user["nickname"]},
                {"token", token}
            };
            return crow::response(201, utils::Success(result).dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // POST /api/auth/login - 登录
    CROW_ROUTE(app, "/api/auth/login").methods("POST"_method)
    ([&db](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string username = body.value("username", std::string(""));
            std::string password = body.value("password", std::string(""));

            if (username.empty() || password.empty()) {
                return crow::response(400, utils::Error("用户名和密码不能为空").dump());
            }

            auto user = db.QueryOne("SELECT id, username, password, nickname FROM users WHERE username = ?", {username});
            if (user.is_null()) {
                return crow::response(400, utils::Error("用户不存在").dump());
            }

            // 验证密码
            std::string salted = password + "mindvault_salt_2026";
            std::hash<std::string> hasher;
            size_t hash = hasher(salted);
            std::string hashed = std::to_string(hash);

            if (user.value("password", "") != hashed) {
                return crow::response(400, utils::Error("密码错误").dump());
            }

            // 确保用户数据库存在
            int64_t user_id = user["id"].get<int64_t>();
            UserDbManager::Instance().GetUserDb(user_id);

            // 生成 token
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            std::string token = std::to_string(user_id) + "_" + std::to_string(timestamp) + "_mindvault";

            nlohmann::json result = {
                {"id", user["id"]},
                {"username", user["username"]},
                {"nickname", user["nickname"]},
                {"token", token}
            };
            return crow::response(utils::Success(result).dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // GET /api/auth/me - 获取当前用户信息
    CROW_ROUTE(app, "/api/auth/me").methods("GET"_method)
    ([&db](const crow::request& req) {
        // 从 header 获取 token
        std::string token = req.get_header_value("Authorization");
        if (token.empty() || token.substr(0, 7) != "Bearer ") {
            return crow::response(401, utils::Error("未登录").dump());
        }
        token = token.substr(7);

        // 解析 token 获取 user_id
        auto pos = token.find('_');
        if (pos == std::string::npos) {
            return crow::response(401, utils::Error("无效的 token").dump());
        }
        int64_t user_id = std::stoll(token.substr(0, pos));

        auto user = db.QueryOne("SELECT id, username, nickname, created_at FROM users WHERE id = ?", {user_id});
        if (user.is_null()) {
            return crow::response(401, utils::Error("用户不存在").dump());
        }

        return crow::response(utils::Success(user).dump());
    });

    // GET /api/auth/user-db - 获取用户数据库（供内部使用）
    CROW_ROUTE(app, "/api/auth/user-db").methods("GET"_method)
    ([&db](const crow::request& req) {
        std::string token = req.get_header_value("Authorization");
        if (token.empty() || token.substr(0, 7) != "Bearer ") {
            return crow::response(401, utils::Error("未登录").dump());
        }
        token = token.substr(7);

        auto pos = token.find('_');
        if (pos == std::string::npos) {
            return crow::response(401, utils::Error("无效的 token").dump());
        }
        int64_t user_id = std::stoll(token.substr(0, pos));

        auto user_db = UserDbManager::Instance().GetUserDb(user_id);
        nlohmann::json result = {{"user_id", user_id}, {"db_path", "data/user_" + std::to_string(user_id) + ".db"}};
        return crow::response(utils::Success(result).dump());
    });
}

}

