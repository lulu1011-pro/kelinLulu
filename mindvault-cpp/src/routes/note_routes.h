#pragma once
// Note routes: /api/notes/*

#include "../database.h"
#include "../services/note_service.h"
#include "../services/tag_service.h"
#include "../utils/response.h"
#include "user_routes.h"
#include "crow_all.h"
#include <nlohmann/json.hpp>
#include <memory>

namespace mindvault::routes {

// 从 token 获取用户 ID
inline int64_t GetUserIdFromToken(const crow::request& req) {
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

// 获取用户数据库
inline std::shared_ptr<Database> GetUserDb(const crow::request& req) {
    int64_t user_id = GetUserIdFromToken(req);
    if (user_id == 0) return nullptr;
    return UserDbManager::Instance().GetUserDb(user_id);
}

inline void RegisterNoteRoutes(crow::App<>& app, Database& db) {
    // 默认服务（用于兼容）
    auto note_svc = std::make_shared<services::NoteService>(db);
    auto tag_svc  = std::make_shared<services::TagService>(db);

    // GET /api/notes - list notes
    CROW_ROUTE(app, "/api/notes").methods("GET"_method)
    ([&db](const crow::request& req) {
        auto user_db = GetUserDb(req);
        if (!user_db) {
            return utils::JsonResp(utils::Error("未登录"), 401);
        }
        services::NoteService svc(*user_db);
        std::string folder = req.url_params.get("folder") ? req.url_params.get("folder") : "";
        auto list = svc.List(folder);
        return utils::JsonResp(utils::Success(list));
    });

    // GET /api/notes/:id - get note detail
    CROW_ROUTE(app, "/api/notes/<int>").methods("GET"_method)
    ([&db](const crow::request& req, int64_t id) {
        auto user_db = GetUserDb(req);
        if (!user_db) {
            return utils::JsonResp(utils::Error("未登录"), 401);
        }
        services::NoteService svc(*user_db);
        auto note = svc.GetById(id);
        if (note.is_null()) {
            return utils::JsonResp(utils::NotFound("Note"), 404);
        }
        return utils::JsonResp(utils::Success(note));
    });

    // POST /api/notes - create note
    CROW_ROUTE(app, "/api/notes").methods("POST"_method)
    ([&db](const crow::request& req) {
        try {
            auto user_db = GetUserDb(req);
            if (!user_db) {
                return utils::JsonResp(utils::Error("未登录"), 401);
            }
            services::NoteService svc(*user_db);
            auto body = nlohmann::json::parse(req.body);
            std::string title   = body.value("title", std::string("Untitled"));
            std::string content = body.value("content", std::string(""));
            std::string folder  = body.value("folder", std::string("default"));
            // P1-5: 可选的 embedding API 配置（前端有配置就传，没有就只切块不调 API）
            std::string api_url = body.value("api_url", std::string(""));
            std::string api_key = body.value("api_key", std::string(""));
            std::string model   = body.value("model", std::string(""));
            auto note = svc.Create(title, content, folder, api_url, api_key, model);
            return utils::JsonResp(utils::Success(note), 201);
        } catch (const std::exception& e) {
            return utils::JsonResp(utils::Error(e.what()), 400);
        }
    });

    // PUT /api/notes/:id - update note
    CROW_ROUTE(app, "/api/notes/<int>").methods("PUT"_method)
    ([&db](const crow::request& req, int64_t id) {
        try {
            auto user_db = GetUserDb(req);
            if (!user_db) {
                return utils::JsonResp(utils::Error("未登录"), 401);
            }
            services::NoteService svc(*user_db);
            auto body = nlohmann::json::parse(req.body);
            std::string title   = body.value("title", std::string(""));
            std::string content = body.value("content", std::string(""));
            std::string folder  = body.value("folder", std::string(""));
            // P1-5: 可选的 embedding API 配置
            std::string api_url = body.value("api_url", std::string(""));
            std::string api_key = body.value("api_key", std::string(""));
            std::string model   = body.value("model", std::string(""));
            auto note = svc.Update(id, title, content, folder, api_url, api_key, model);
            if (note.is_null()) {
                return utils::JsonResp(utils::NotFound("Note"), 404);
            }
            return utils::JsonResp(utils::Success(note));
        } catch (const std::exception& e) {
            return utils::JsonResp(utils::Error(e.what()), 400);
        }
    });

    // DELETE /api/notes/:id - soft delete
    CROW_ROUTE(app, "/api/notes/<int>").methods("DELETE"_method)
    ([&db](const crow::request& req, int64_t id) {
        auto user_db = GetUserDb(req);
        if (!user_db) {
            return utils::JsonResp(utils::Error("未登录"), 401);
        }
        services::NoteService svc(*user_db);
        bool ok = svc.Delete(id);
        if (!ok) {
            return utils::JsonResp(utils::NotFound("Note"), 404);
        }
        return utils::JsonResp(utils::Success());
    });

    // GET /api/notes/trash - list deleted notes
    CROW_ROUTE(app, "/api/notes/trash").methods("GET"_method)
    ([&db](const crow::request& req) {
        auto user_db = GetUserDb(req);
        if (!user_db) {
            return utils::JsonResp(utils::Error("未登录"), 401);
        }
        services::NoteService svc(*user_db);
        auto list = svc.ListTrash();
        return utils::JsonResp(utils::Success(list));
    });

    // POST /api/notes/:id/restore - restore deleted note
    CROW_ROUTE(app, "/api/notes/<int>/restore").methods("POST"_method)
    ([&db](const crow::request& req, int64_t id) {
        auto user_db = GetUserDb(req);
        if (!user_db) {
            return utils::JsonResp(utils::Error("未登录"), 401);
        }
        services::NoteService svc(*user_db);
        auto note = svc.Restore(id);
        if (note.is_null()) {
            return utils::JsonResp(utils::NotFound("Note in trash"), 404);
        }
        return utils::JsonResp(utils::Success(note));
    });

    // DELETE /api/notes/:id/permanent - permanent delete
    CROW_ROUTE(app, "/api/notes/<int>/permanent").methods("DELETE"_method)
    ([&db](const crow::request& req, int64_t id) {
        auto user_db = GetUserDb(req);
        if (!user_db) {
            return utils::JsonResp(utils::Error("未登录"), 401);
        }
        services::NoteService svc(*user_db);
        bool ok = svc.PermanentDelete(id);
        if (!ok) {
            return utils::JsonResp(utils::NotFound("Note in trash"), 404);
        }
        return utils::JsonResp(utils::Success());
    });

    // DELETE /api/notes/trash/empty - empty trash
    CROW_ROUTE(app, "/api/notes/trash/empty").methods("DELETE"_method)
    ([&db](const crow::request& req) {
        auto user_db = GetUserDb(req);
        if (!user_db) {
            return utils::JsonResp(utils::Error("未登录"), 401);
        }
        services::NoteService svc(*user_db);
        int count = svc.EmptyTrash();
        return utils::JsonResp(utils::Success(nlohmann::json({{"deleted_count", count}})));
    });

    // GET /api/notes/:id/backlinks - get backlinks
    CROW_ROUTE(app, "/api/notes/<int>/backlinks").methods("GET"_method)
    ([&db](const crow::request& req, int64_t id) {
        auto user_db = GetUserDb(req);
        if (!user_db) {
            return utils::JsonResp(utils::Error("未登录"), 401);
        }
        services::NoteService svc(*user_db);
        auto links = svc.GetBacklinks(id);
        return utils::JsonResp(utils::Success(links));
    });

    // GET /api/notes/:id/links - get forward links
    CROW_ROUTE(app, "/api/notes/<int>/links").methods("GET"_method)
    ([&db](const crow::request& req, int64_t id) {
        auto user_db = GetUserDb(req);
        if (!user_db) {
            return utils::JsonResp(utils::Error("未登录"), 401);
        }
        services::NoteService svc(*user_db);
        auto links = svc.GetForwardLinks(id);
        return utils::JsonResp(utils::Success(links));
    });


    // GET /api/notes/:id/recommendations - P1-7 关联笔记推荐（link_edges 强信号 + 向量弱信号融合）
    CROW_ROUTE(app, "/api/notes/<int>/recommendations").methods("GET"_method)
    ([&db](const crow::request& req, int64_t id) {
        auto user_db = GetUserDb(req);
        if (!user_db) {
            return utils::JsonResp(utils::Error("未登录"), 401);
        }
        services::NoteService svc(*user_db);
        // 先确认笔记存在
        auto note = svc.GetById(id);
        if (note.is_null()) {
            return utils::JsonResp(utils::Error("笔记不存在"), 404);
        }
        auto recs = svc.GetRecommendations(id);
        return utils::JsonResp(utils::Success(recs));
    });

    // PUT /api/notes/:id/order - update note sort order
    CROW_ROUTE(app, "/api/notes/<int>/order").methods("PUT"_method)
    ([&db](const crow::request& req, int64_t id) {
        try {
            auto user_db = GetUserDb(req);
            if (!user_db) {
                return utils::JsonResp(utils::Error("未登录"), 401);
            }
            services::NoteService svc(*user_db);
            auto body = nlohmann::json::parse(req.body);
            int sort_order = body.value("sort_order", 0);
            bool ok = svc.UpdateSortOrder(id, sort_order);
            if (!ok) {
                return utils::JsonResp(utils::NotFound("Note"), 404);
            }
            return utils::JsonResp(utils::Success());
        } catch (const std::exception& e) {
            return utils::JsonResp(utils::Error(e.what()), 400);
        }
    });

    // PUT /api/notes/order - batch update sort orders
    CROW_ROUTE(app, "/api/notes/order").methods("PUT"_method)
    ([&db](const crow::request& req) {
        try {
            auto user_db = GetUserDb(req);
            if (!user_db) {
                return utils::JsonResp(utils::Error("未登录"), 401);
            }
            services::NoteService svc(*user_db);
            auto body = nlohmann::json::parse(req.body);
            auto orders_json = body.value("orders", nlohmann::json::array());
            std::vector<std::pair<int64_t, int>> orders;
            for (auto& item : orders_json) {
                int64_t id = item["id"].get<int64_t>();
                int order = item["order"].get<int>();
                orders.push_back({id, order});
            }
            svc.BatchUpdateSortOrder(orders);
            return utils::JsonResp(utils::Success());
        } catch (const std::exception& e) {
            return utils::JsonResp(utils::Error(e.what()), 400);
        }
    });

    // GET /api/notes/:id/tags - get note tags
    CROW_ROUTE(app, "/api/notes/<int>/tags").methods("GET"_method)
    ([&db](const crow::request& req, int64_t id) {
        auto user_db = GetUserDb(req);
        if (!user_db) {
            return utils::JsonResp(utils::Error("未登录"), 401);
        }
        services::TagService svc(*user_db);
        auto tags = svc.GetNoteTags(id);
        return utils::JsonResp(utils::Success(tags));
    });

    // POST /api/notes/:id/tags - add tag to note
    CROW_ROUTE(app, "/api/notes/<int>/tags").methods("POST"_method)
    ([&db](const crow::request& req, int64_t id) {
        try {
            auto user_db = GetUserDb(req);
            if (!user_db) {
                return utils::JsonResp(utils::Error("未登录"), 401);
            }
            services::TagService svc(*user_db);
            auto body = nlohmann::json::parse(req.body);
            std::string name = body.value("name", std::string(""));
            if (name.empty()) {
                return utils::JsonResp(utils::Error("Tag name is required"), 400);
            }
            svc.AddTagToNote(id, name);
            return utils::JsonResp(utils::Success());
        } catch (const std::exception& e) {
            return utils::JsonResp(utils::Error(e.what()), 400);
        }
    });

    // DELETE /api/notes/:id/tags - remove tag from note
    CROW_ROUTE(app, "/api/notes/<int>/tags").methods("DELETE"_method)
    ([&db](const crow::request& req, int64_t id) {
        try {
            auto user_db = GetUserDb(req);
            if (!user_db) {
                return utils::JsonResp(utils::Error("未登录"), 401);
            }
            services::TagService svc(*user_db);
            auto body = nlohmann::json::parse(req.body);
            std::string name = body.value("name", std::string(""));
            if (name.empty()) {
                return utils::JsonResp(utils::Error("Tag name is required"), 400);
            }
            bool ok = svc.RemoveTagFromNote(id, name);
            if (!ok) {
                return utils::JsonResp(utils::Error("Tag not found on note"), 404);
            }
            return utils::JsonResp(utils::Success());
        } catch (const std::exception& e) {
            return utils::JsonResp(utils::Error(e.what()), 400);
        }
    });

    // GET /api/tags/:name/notes - get notes by tag name
    CROW_ROUTE(app, "/api/tags/<str>/notes").methods("GET"_method)
    ([&db](const crow::request& req, const std::string& tag_name) {
        auto user_db = GetUserDb(req);
        if (!user_db) {
            return utils::JsonResp(utils::Error("未登录"), 401);
        }
        services::TagService svc(*user_db);
        auto notes = svc.GetNotesByTag(tag_name);
        return utils::JsonResp(utils::Success(notes));
    });

    // GET /api/notes/:id/versions - get version history
    CROW_ROUTE(app, "/api/notes/<int>/versions").methods("GET"_method)
    ([&db](const crow::request& req, int64_t id) {
        auto user_db = GetUserDb(req);
        if (!user_db) {
            return utils::JsonResp(utils::Error("未登录"), 401);
        }
        services::NoteService svc(*user_db);
        auto versions = svc.GetVersions(id);
        return utils::JsonResp(utils::Success(versions));
    });

    // GET /api/versions/:id - get version detail
    CROW_ROUTE(app, "/api/versions/<int>").methods("GET"_method)
    ([&db](const crow::request& req, int64_t id) {
        auto user_db = GetUserDb(req);
        if (!user_db) {
            return utils::JsonResp(utils::Error("未登录"), 401);
        }
        services::NoteService svc(*user_db);
        auto version = svc.GetVersion(id);
        if (version.is_null()) {
            return utils::JsonResp(utils::NotFound("Version"), 404);
        }
        return utils::JsonResp(utils::Success(version));
    });
}

} // namespace mindvault::routes
