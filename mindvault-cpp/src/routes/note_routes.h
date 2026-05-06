#pragma once
// Note routes: /api/notes/*

#include "../database.h"
#include "../services/note_service.h"
#include "../services/tag_service.h"
#include "../utils/response.h"
#include "crow_all.h"
#include <nlohmann/json.hpp>
#include <memory>

namespace mindvault::routes {

inline void RegisterNoteRoutes(crow::App<>& app, Database& db) {
    // Use shared_ptr to ensure services outlive the route registration scope
    auto note_svc = std::make_shared<services::NoteService>(db);
    auto tag_svc  = std::make_shared<services::TagService>(db);

    // GET /api/notes - list notes
    CROW_ROUTE(app, "/api/notes").methods("GET"_method)
    ([note_svc](const crow::request& req) {
        std::string folder = req.url_params.get("folder") ? req.url_params.get("folder") : "";
        auto list = note_svc->List(folder);
        return crow::response(utils::Success(list).dump());
    });

    // GET /api/notes/:id - get note detail
    CROW_ROUTE(app, "/api/notes/<int>").methods("GET"_method)
    ([note_svc](int64_t id) {
        auto note = note_svc->GetById(id);
        if (note.is_null()) {
            return crow::response(404, utils::NotFound("Note").dump());
        }
        return crow::response(utils::Success(note).dump());
    });

    // POST /api/notes - create note
    CROW_ROUTE(app, "/api/notes").methods("POST"_method)
    ([note_svc](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string title   = body.value("title", std::string("Untitled"));
            std::string content = body.value("content", std::string(""));
            std::string folder  = body.value("folder", std::string("default"));
            auto note = note_svc->Create(title, content, folder);
            return crow::response(201, utils::Success(note).dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // PUT /api/notes/:id - update note
    CROW_ROUTE(app, "/api/notes/<int>").methods("PUT"_method)
    ([note_svc](const crow::request& req, int64_t id) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string title   = body.value("title", std::string(""));
            std::string content = body.value("content", std::string(""));
            std::string folder  = body.value("folder", std::string(""));
            auto note = note_svc->Update(id, title, content, folder);
            if (note.is_null()) {
                return crow::response(404, utils::NotFound("Note").dump());
            }
            return crow::response(utils::Success(note).dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // DELETE /api/notes/:id - soft delete
    CROW_ROUTE(app, "/api/notes/<int>").methods("DELETE"_method)
    ([note_svc](int64_t id) {
        bool ok = note_svc->Delete(id);
        if (!ok) {
            return crow::response(404, utils::NotFound("Note").dump());
        }
        return crow::response(utils::Success().dump());
    });

    // GET /api/notes/trash - list deleted notes
    CROW_ROUTE(app, "/api/notes/trash").methods("GET"_method)
    ([note_svc]() {
        auto list = note_svc->ListTrash();
        return crow::response(utils::Success(list).dump());
    });

    // POST /api/notes/:id/restore - restore deleted note
    CROW_ROUTE(app, "/api/notes/<int>/restore").methods("POST"_method)
    ([note_svc](int64_t id) {
        auto note = note_svc->Restore(id);
        if (note.is_null()) {
            return crow::response(404, utils::NotFound("Note in trash").dump());
        }
        return crow::response(utils::Success(note).dump());
    });

    // DELETE /api/notes/:id/permanent - permanent delete
    CROW_ROUTE(app, "/api/notes/<int>/permanent").methods("DELETE"_method)
    ([note_svc](int64_t id) {
        bool ok = note_svc->PermanentDelete(id);
        if (!ok) {
            return crow::response(404, utils::NotFound("Note in trash").dump());
        }
        return crow::response(utils::Success().dump());
    });

    // DELETE /api/notes/trash/empty - empty trash
    CROW_ROUTE(app, "/api/notes/trash/empty").methods("DELETE"_method)
    ([note_svc]() {
        int count = note_svc->EmptyTrash();
        return crow::response(utils::Success(nlohmann::json({{"deleted_count", count}})).dump());
    });

    // GET /api/notes/:id/backlinks - get backlinks
    CROW_ROUTE(app, "/api/notes/<int>/backlinks").methods("GET"_method)
    ([note_svc](int64_t id) {
        auto links = note_svc->GetBacklinks(id);
        return crow::response(utils::Success(links).dump());
    });

    // GET /api/notes/:id/links - get forward links
    CROW_ROUTE(app, "/api/notes/<int>/links").methods("GET"_method)
    ([note_svc](int64_t id) {
        auto links = note_svc->GetForwardLinks(id);
        return crow::response(utils::Success(links).dump());
    });

    // PUT /api/notes/:id/order - update note sort order
    CROW_ROUTE(app, "/api/notes/<int>/order").methods("PUT"_method)
    ([note_svc](const crow::request& req, int64_t id) {
        try {
            auto body = nlohmann::json::parse(req.body);
            int sort_order = body.value("sort_order", 0);
            bool ok = note_svc->UpdateSortOrder(id, sort_order);
            if (!ok) {
                return crow::response(404, utils::NotFound("Note").dump());
            }
            return crow::response(utils::Success().dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // PUT /api/notes/order - batch update sort orders
    CROW_ROUTE(app, "/api/notes/order").methods("PUT"_method)
    ([note_svc](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body);
            auto orders_json = body.value("orders", nlohmann::json::array());
            std::vector<std::pair<int64_t, int>> orders;
            for (auto& item : orders_json) {
                int64_t id = item["id"].get<int64_t>();
                int order = item["order"].get<int>();
                orders.push_back({id, order});
            }
            note_svc->BatchUpdateSortOrder(orders);
            return crow::response(utils::Success().dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // GET /api/notes/:id/tags - get note tags
    CROW_ROUTE(app, "/api/notes/<int>/tags").methods("GET"_method)
    ([tag_svc](int64_t id) {
        auto tags = tag_svc->GetNoteTags(id);
        return crow::response(utils::Success(tags).dump());
    });

    // POST /api/notes/:id/tags - add tag to note
    CROW_ROUTE(app, "/api/notes/<int>/tags").methods("POST"_method)
    ([tag_svc](const crow::request& req, int64_t id) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string name = body.value("name", std::string(""));
            if (name.empty()) {
                return crow::response(400, utils::Error("Tag name is required").dump());
            }
            tag_svc->AddTagToNote(id, name);
            return crow::response(utils::Success().dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // DELETE /api/notes/:id/tags - remove tag from note
    CROW_ROUTE(app, "/api/notes/<int>/tags").methods("DELETE"_method)
    ([tag_svc](const crow::request& req, int64_t id) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string name = body.value("name", std::string(""));
            if (name.empty()) {
                return crow::response(400, utils::Error("Tag name is required").dump());
            }
            bool ok = tag_svc->RemoveTagFromNote(id, name);
            if (!ok) {
                return crow::response(404, utils::Error("Tag not found on note").dump());
            }
            return crow::response(utils::Success().dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // GET /api/tags/:name/notes - get notes by tag name
    CROW_ROUTE(app, "/api/tags/<str>/notes").methods("GET"_method)
    ([tag_svc](const std::string& tag_name) {
        auto notes = tag_svc->GetNotesByTag(tag_name);
        return crow::response(utils::Success(notes).dump());
    });

    // GET /api/notes/:id/versions - get version history
    CROW_ROUTE(app, "/api/notes/<int>/versions").methods("GET"_method)
    ([note_svc](int64_t id) {
        auto versions = note_svc->GetVersions(id);
        return crow::response(utils::Success(versions).dump());
    });

    // GET /api/versions/:id - get version detail
    CROW_ROUTE(app, "/api/versions/<int>").methods("GET"_method)
    ([note_svc](int64_t id) {
        auto version = note_svc->GetVersion(id);
        if (version.is_null()) {
            return crow::response(404, utils::NotFound("Version").dump());
        }
        return crow::response(utils::Success(version).dump());
    });
}

} // namespace mindvault::routes
