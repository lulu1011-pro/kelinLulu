#pragma once
#include "../services/flashcard_service.h"
#include "../utils/response.h"
#include "user_routes.h"
#include "crow_all.h"
#include <memory>

namespace mindvault::routes {

// 从 token 获取用户数据库
inline std::shared_ptr<Database> GetFlashcardUserDb(const crow::request& req) {
    std::string token = req.get_header_value("Authorization");
    if (token.empty() || token.substr(0, 7) != "Bearer ") return nullptr;
    token = token.substr(7);
    auto pos = token.find('_');
    if (pos == std::string::npos) return nullptr;
    try {
        int64_t user_id = std::stoll(token.substr(0, pos));
        return UserDbManager::Instance().GetUserDb(user_id);
    } catch (...) {
        return nullptr;
    }
}

inline void RegisterFlashcardRoutes(crow::App<>& app, Database& db) {
    // GET /api/notes/:id/flashcards - get flashcards for note
    CROW_ROUTE(app, "/api/notes/<int>/flashcards").methods("GET"_method)
    ([&db](const crow::request& req, int64_t note_id) {
        auto user_db = GetFlashcardUserDb(req);
        if (!user_db) return crow::response(401, utils::Error("未登录").dump());
        services::FlashcardService svc(*user_db);
        auto cards = svc.GetByNote(note_id);
        return crow::response(utils::Success(cards).dump());
    });

    // POST /api/flashcards - create flashcard
    CROW_ROUTE(app, "/api/flashcards").methods("POST"_method)
    ([&db](const crow::request& req) {
        try {
            auto user_db = GetFlashcardUserDb(req);
            if (!user_db) return crow::response(401, utils::Error("未登录").dump());
            services::FlashcardService svc(*user_db);
            auto body = nlohmann::json::parse(req.body);
            int64_t note_id = body.value("note_id", 0);
            std::string front = body.value("front", std::string(""));
            std::string back = body.value("back", std::string(""));
            if (note_id == 0 || front.empty() || back.empty()) {
                return crow::response(400, utils::Error("note_id, front, back are required").dump());
            }
            auto card = svc.Create(note_id, front, back);
            return crow::response(201, utils::Success(card).dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // PUT /api/flashcards/:id - update flashcard
    CROW_ROUTE(app, "/api/flashcards/<int>").methods("PUT"_method)
    ([&db](const crow::request& req, int64_t id) {
        try {
            auto user_db = GetFlashcardUserDb(req);
            if (!user_db) return crow::response(401, utils::Error("未登录").dump());
            services::FlashcardService svc(*user_db);
            auto body = nlohmann::json::parse(req.body);
            std::string front = body.value("front", std::string(""));
            std::string back = body.value("back", std::string(""));
            auto card = svc.Update(id, front, back);
            return crow::response(utils::Success(card).dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // DELETE /api/flashcards/:id - delete flashcard
    CROW_ROUTE(app, "/api/flashcards/<int>").methods("DELETE"_method)
    ([&db](const crow::request& req, int64_t id) {
        auto user_db = GetFlashcardUserDb(req);
        if (!user_db) return crow::response(401, utils::Error("未登录").dump());
        services::FlashcardService svc(*user_db);
        svc.Delete(id);
        return crow::response(utils::Success().dump());
    });

    // GET /api/flashcards/due - get due flashcards
    CROW_ROUTE(app, "/api/flashcards/due").methods("GET"_method)
    ([&db](const crow::request& req) {
        auto user_db = GetFlashcardUserDb(req);
        if (!user_db) return crow::response(401, utils::Error("未登录").dump());
        services::FlashcardService svc(*user_db);
        auto cards = svc.GetDue();
        return crow::response(utils::Success(cards).dump());
    });

    // POST /api/flashcards/:id/review - submit review
    CROW_ROUTE(app, "/api/flashcards/<int>/review").methods("POST"_method)
    ([&db](const crow::request& req, int64_t id) {
        try {
            auto user_db = GetFlashcardUserDb(req);
            if (!user_db) return crow::response(401, utils::Error("未登录").dump());
            services::FlashcardService svc(*user_db);
            auto body = nlohmann::json::parse(req.body);
            int quality = body.value("quality", 3);
            auto card = svc.Review(id, quality);
            return crow::response(utils::Success(card).dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });
}

}
