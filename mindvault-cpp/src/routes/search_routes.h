#pragma once
// Search routes: /api/search

#include "../database.h"
#include "../services/search_service.h"
#include "../utils/response.h"
#include "crow_all.h"
#include <memory>

namespace mindvault::routes {

inline void RegisterSearchRoutes(crow::App<>& app, Database& db) {
    auto search_svc = std::make_shared<services::SearchService>(db);

    // GET /api/search?q=keyword - FTS5 full-text search
    CROW_ROUTE(app, "/api/search").methods("GET"_method)
    ([search_svc](const crow::request& req) {
        const char* q = req.url_params.get("q");
        if (!q || std::string(q).empty()) {
            return crow::response(400, utils::Error("Search query 'q' is required").dump());
        }
        auto results = search_svc->Search(q);
        return crow::response(utils::Success(results).dump());
    });

    // GET /api/search/title?keyword=xxx - fuzzy search by title
    CROW_ROUTE(app, "/api/search/title").methods("GET"_method)
    ([search_svc](const crow::request& req) {
        const char* kw = req.url_params.get("keyword");
        if (!kw || std::string(kw).empty()) {
            return crow::response(400, utils::Error("Keyword is required").dump());
        }
        auto results = search_svc->SearchByTitle(kw);
        return crow::response(utils::Success(results).dump());
    });
}

} // namespace mindvault::routes
