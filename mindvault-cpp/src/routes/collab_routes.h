#pragma once
// Collaboration routes: REST API for real-time editing status
// Uses polling instead of WebSocket (Crow version limitation)

#include "../utils/response.h"
#include "crow_all.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <chrono>

namespace mindvault::routes {

// 从 token 解析 user_id
inline int64_t CollabUserId(const crow::request& req) {
    std::string token = req.get_header_value("Authorization");
    if (token.empty() || token.substr(0, 7) != "Bearer ") return 0;
    token = token.substr(7);
    auto pos = token.find('_');
    if (pos == std::string::npos) return 0;
    try { return std::stoll(token.substr(0, pos)); } catch (...) { return 0; }
}

// 协作用户管理
class CollabState {
public:
    static CollabState& Instance() {
        static CollabState inst;
        return inst;
    }

    // 用户加入
    void Join(int64_t note_id, int64_t user_id, const std::string& username) {
        std::lock_guard<std::mutex> lock(mutex_);
        viewers_[note_id][user_id] = {username, Tick()};
    }

    // 用户离开
    void Leave(int64_t note_id, int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = viewers_.find(note_id);
        if (it != viewers_.end()) {
            it->second.erase(user_id);
            if (it->second.empty()) viewers_.erase(it);
        }
    }

    // 获取在线用户
    nlohmann::json GetViewers(int64_t note_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        Purge();
        nlohmann::json result = nlohmann::json::array();
        auto it = viewers_.find(note_id);
        if (it != viewers_.end()) {
            for (auto& [uid, info] : it->second) {
                nlohmann::json u;
                u["id"] = uid;
                u["username"] = info.username;
                result.push_back(u);
            }
        }
        return result;
    }

    // 获取在线用户数
    int GetCount(int64_t note_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        Purge();
        auto it = viewers_.find(note_id);
        return it != viewers_.end() ? (int)it->second.size() : 0;
    }

    // 刷新心跳
    void Heartbeat(int64_t note_id, int64_t user_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = viewers_.find(note_id);
        if (it != viewers_.end()) {
            auto uit = it->second.find(user_id);
            if (uit != it->second.end()) {
                uit->second.last_seen = Tick();
            }
        }
    }

private:
    struct UserInfo {
        std::string username;
        int64_t last_seen;
    };
    std::unordered_map<int64_t, std::unordered_map<int64_t, UserInfo>> viewers_;
    std::mutex mutex_;

    int64_t Tick() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // 清除超时用户（30秒无心跳）
    void Purge() {
        int64_t now = Tick();
        for (auto it = viewers_.begin(); it != viewers_.end(); ) {
            for (auto uit = it->second.begin(); uit != it->second.end(); ) {
                if (now - uit->second.last_seen > 30) {
                    uit = it->second.erase(uit);
                } else {
                    ++uit;
                }
            }
            if (it->second.empty()) it = viewers_.erase(it);
            else ++it;
        }
    }
};

inline void RegisterCollabRoutes(crow::App<>& app, Database& db) {
    // POST /api/notes/:id/join - 加入笔记编辑
    CROW_ROUTE(app, "/api/notes/<int>/join").methods("POST"_method)
    ([&db](const crow::request& req, int64_t note_id) {
        int64_t user_id = CollabUserId(req);
        if (user_id == 0) return crow::response(401, utils::Error("未登录").dump());
        auto user = db.QueryOne("SELECT nickname, username FROM users WHERE id = ?", {user_id});
        std::string name = user.value("nickname", user.value("username", ""));
        CollabState::Instance().Join(note_id, user_id, name);
        return crow::response(utils::Success().dump());
    });

    // POST /api/notes/:id/leave - 离开笔记编辑
    CROW_ROUTE(app, "/api/notes/<int>/leave").methods("POST"_method)
    ([](const crow::request& req, int64_t note_id) {
        int64_t user_id = CollabUserId(req);
        if (user_id == 0) return crow::response(401, utils::Error("未登录").dump());
        CollabState::Instance().Leave(note_id, user_id);
        return crow::response(utils::Success().dump());
    });

    // GET /api/notes/:id/viewers - 获取在线用户
    CROW_ROUTE(app, "/api/notes/<int>/viewers").methods("GET"_method)
    ([](const crow::request& req, int64_t note_id) {
        int64_t user_id = CollabUserId(req);
        if (user_id == 0) return crow::response(401, utils::Error("未登录").dump());
        auto viewers = CollabState::Instance().GetViewers(note_id);
        return crow::response(utils::Success(viewers).dump());
    });

    // POST /api/notes/:id/heartbeat - 心跳
    CROW_ROUTE(app, "/api/notes/<int>/heartbeat").methods("POST"_method)
    ([](const crow::request& req, int64_t note_id) {
        int64_t user_id = CollabUserId(req);
        if (user_id == 0) return crow::response(401, utils::Error("未登录").dump());
        CollabState::Instance().Heartbeat(note_id, user_id);
        return crow::response(utils::Success().dump());
    });
}

}
