#pragma once
// 用户服务：注册、登录、用户管理

#include "../database.h"
#include <string>
#include <nlohmann/json.hpp>

namespace mindvault::services {

class UserService {
public:
    explicit UserService(Database& db) : db_(db) {}

    // 初始化用户表
    static void InitTables(Database& db) {
        db.Execute(R"(
            CREATE TABLE IF NOT EXISTS users (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                username    TEXT NOT NULL UNIQUE,
                password    TEXT NOT NULL,
                nickname    TEXT DEFAULT '',
                avatar      TEXT DEFAULT '',
                created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime')),
                updated_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
            )
        )");
    }

    // 注册
    nlohmann::json Register(const std::string& username, const std::string& password, const std::string& nickname = "") {
        // 检查用户名是否已存在
        auto existing = db_.QueryOne("SELECT id FROM users WHERE username = ?", {username});
        if (!existing.is_null()) {
            return {{"ok", false}, {"error", "用户名已存在"}};
        }

        // 简单密码哈希（实际应用应使用 bcrypt）
        std::string hashed = HashPassword(password);

        db_.Execute(
            "INSERT INTO users (username, password, nickname) VALUES (?, ?, ?)",
            {username, hashed, nickname.empty() ? username : nickname}
        );

        int64_t user_id = db_.LastInsertId();

        // 创建用户独立数据库
        CreateUserDatabase(user_id);

        auto user = db_.QueryOne("SELECT id, username, nickname, created_at FROM users WHERE id = ?", {user_id});
        return {{"ok", true}, {"data", user}};
    }

    // 登录
    nlohmann::json Login(const std::string& username, const std::string& password) {
        auto user = db_.QueryOne("SELECT id, username, password, nickname FROM users WHERE username = ?", {username});
        if (user.is_null()) {
            return {{"ok", false}, {"error", "用户不存在"}};
        }

        std::string stored_hash = user.value("password", "");
        if (stored_hash != HashPassword(password)) {
            return {{"ok", false}, {"error", "密码错误"}};
        }

        // 生成简单 token（实际应用应使用 JWT）
        std::string token = GenerateToken(user["id"].get<int64_t>());

        nlohmann::json result = {
            {"id", user["id"]},
            {"username", user["username"]},
            {"nickname", user["nickname"]},
            {"token", token}
        };
        return {{"ok", true}, {"data", result}};
    }

    // 获取用户信息
    nlohmann::json GetUser(int64_t user_id) {
        auto user = db_.QueryOne("SELECT id, username, nickname, created_at FROM users WHERE id = ?", {user_id});
        if (user.is_null()) {
            return {{"ok", false}, {"error", "用户不存在"}};
        }
        return {{"ok", true}, {"data", user}};
    }

    // 获取用户数据库路径
    std::string GetUserDbPath(int64_t user_id) {
        return "data/user_" + std::to_string(user_id) + ".db";
    }

    // 创建用户独立数据库
    void CreateUserDatabase(int64_t user_id) {
        std::string db_path = GetUserDbPath(user_id);
        Database user_db(db_path);
        user_db.Init();
    }

private:
    Database& db_;

    // 简单密码哈希（实际应用应使用 bcrypt/argon2）
    std::string HashPassword(const std::string& password) {
        // 简单哈希：password + "mindvault_salt"
        std::string salted = password + "mindvault_salt_2026";
        std::hash<std::string> hasher;
        size_t hash = hasher(salted);
        return std::to_string(hash);
    }

    // 生成简单 token
    std::string GenerateToken(int64_t user_id) {
        // 简单 token：user_id + timestamp + hash
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
        return std::to_string(user_id) + "_" + std::to_string(timestamp) + "_mindvault";
    }
};

}
