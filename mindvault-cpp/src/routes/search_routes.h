#pragma once
// Search routes: /api/search

#include "../database.h"
#include "../services/search_service.h"
#include "../utils/response.h"
#include "user_routes.h"
#include "crow_all.h"
#include <memory>

namespace mindvault::routes {

// 从 token 获取用户 ID（与 note_routes.h 相同）
inline int64_t GetUserIdFromToken2(const crow::request& req) {
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

inline std::shared_ptr<Database> GetUserDb2(const crow::request& req) {
    int64_t user_id = GetUserIdFromToken2(req);
    if (user_id == 0) return nullptr;
    return UserDbManager::Instance().GetUserDb(user_id);
}

inline void RegisterSearchRoutes(crow::App<>& app, Database& db) {
    // GET /api/search?q=keyword - P1-5 混合检索（FTS5 + 向量 + RRF）
    // 可选参数：api_key, api_url, model（为空时降级纯 FTS5）
    CROW_ROUTE(app, "/api/search").methods("GET"_method)
    ([&db](const crow::request& req) {
        auto user_db = GetUserDb2(req);
        if (!user_db) {
            return crow::response(401, utils::Error("未登录").dump());
        }
        services::SearchService svc(*user_db);

        const char* q = req.url_params.get("q");
        if (!q || std::string(q).empty()) {
            return crow::response(400, utils::Error("Search query 'q' is required").dump());
        }

        // 可选的 embedding API 配置（前端有配置就传，没有就纯 FTS5）
        std::string api_key, api_url, model;
        const char* k = req.url_params.get("api_key");
        const char* u = req.url_params.get("api_url");
        const char* m = req.url_params.get("model");
        if (k) api_key = k;
        if (u) api_url = u;
        if (m) model = m;

        auto results = svc.HybridSearch(q, 50, api_url, api_key, model);
        return crow::response(utils::Success(results).dump());
    });

    // GET /api/search/title?keyword=xxx - fuzzy search by title
    CROW_ROUTE(app, "/api/search/title").methods("GET"_method)
    ([&db](const crow::request& req) {
        auto user_db = GetUserDb2(req);
        if (!user_db) {
            return crow::response(401, utils::Error("未登录").dump());
        }
        services::SearchService svc(*user_db);

        const char* kw = req.url_params.get("keyword");
        if (!kw || std::string(kw).empty()) {
            return crow::response(400, utils::Error("Keyword is required").dump());
        }
        auto results = svc.SearchByTitle(kw);
        return crow::response(utils::Success(results).dump());
    });
}

} // namespace mindvault::routes
