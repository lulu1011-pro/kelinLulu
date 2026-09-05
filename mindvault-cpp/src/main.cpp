// MindVault -- Local Knowledge Base
// C++ backend entry: start Crow HTTP server

#include "database.h"
#include "routes/note_routes.h"
#include "routes/search_routes.h"
#include "routes/file_routes.h"
#include "routes/ai_routes.h"
#include "routes/flashcard_routes.h"
#include "routes/user_routes.h"
#include "routes/share_routes.h"
#include "routes/collab_routes.h"
#include "routes/perm_routes.h"
#include "utils/config.h"
#include "crow_all.h"
#include <iostream>
#include <filesystem>
#include <cstdlib>

using namespace mindvault;

int main() {
    try {
    std::cout << "=== MindVault v1.0.0 ===" << std::endl;
    std::cout << "    Local Knowledge Base" << std::endl;
    std::cout << "========================" << std::endl;

    // Init config
    auto& cfg = utils::Config::Instance();
    char* base_env = std::getenv("MINDVAULT_HOME");
    std::filesystem::path base_path;
    if (base_env) {
        base_path = utils::Utf8ToPath(std::string(base_env));
    } else {
        base_path = std::filesystem::current_path();
    }
    cfg.InitFromPath(base_path);
    std::cout << "[Config] Base dir: " << cfg.web_dir_fs.parent_path().string() << std::endl;

    // Init database（用 filesystem::path 打开，支持中文路径）
    Database db(cfg.db_path_fs);
    db.Init();

    // Create Crow app
    crow::App<> app;
    app.loglevel(crow::LogLevel::Info);

    // CORS preflight (OPTIONS)
    CROW_ROUTE(app, "/api/<path>").methods("OPTIONS"_method)
    ([](const crow::request&, std::string) {
        crow::response res;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.code = 204;
        return res;
    });

    // Register routes
    routes::RegisterNoteRoutes(app, db);
    routes::RegisterSearchRoutes(app, db);
    routes::RegisterFileRoutes(app, db);
    routes::RegisterAIRoutes(app, db);
    routes::RegisterFlashcardRoutes(app, db);
    routes::RegisterUserRoutes(app, db);
    routes::RegisterShareRoutes(app, db);
    routes::RegisterCollabRoutes(app, db);
    routes::RegisterPermRoutes(app, db);

    // Static file serving for uploaded images
    auto uploads_dir = cfg.web_dir_fs.parent_path() / "uploads";
    std::filesystem::create_directories(uploads_dir);
    std::cout << "[Static] Uploads dir: " << uploads_dir.string() << std::endl;
    CROW_ROUTE(app, "/uploads/<path>")
    ([uploads_dir](const crow::request&, std::string file_path) {
        auto full_path = uploads_dir / file_path;
        if (!std::filesystem::exists(full_path)) {
            return crow::response(404, "File not found");
        }
        std::ifstream ifs(full_path, std::ios::binary);
        if (!ifs) {
            return crow::response(500, "Cannot read file");
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        crow::response res(content);
        auto ext_pos = file_path.rfind('.');
        if (ext_pos != std::string::npos) {
            std::string ext = file_path.substr(ext_pos + 1);
            if (ext == "png") res.set_header("Content-Type", "image/png");
            else if (ext == "jpg" || ext == "jpeg") res.set_header("Content-Type", "image/jpeg");
            else if (ext == "gif") res.set_header("Content-Type", "image/gif");
            else if (ext == "webp") res.set_header("Content-Type", "image/webp");
            else res.set_header("Content-Type", "application/octet-stream");
        }
        res.set_header("Cache-Control", "public, max-age=86400");
        return res;
    });

    // Static file serving for frontend (web/dist)
    auto dist_dir = cfg.web_dir_fs / "dist";
    if (std::filesystem::exists(dist_dir)) {
        std::cout << "[Static] Serving frontend from: " << dist_dir.string() << std::endl;
        CROW_ROUTE(app, "/app")
        ([dist_dir]() {
            auto index_path = dist_dir / "index.html";
            if (!std::filesystem::exists(index_path)) {
                return crow::response(404, "index.html not found");
            }
            std::ifstream ifs(index_path);
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            crow::response res(content);
            res.set_header("Content-Type", "text/html; charset=utf-8");
            return res;
        });
        CROW_ROUTE(app, "/app/<path>")
        ([dist_dir](const crow::request&, std::string file_path) {
            auto full_path = dist_dir / file_path;
            if (!std::filesystem::exists(full_path)) {
                auto index = dist_dir / "index.html";
                if (std::filesystem::exists(index)) {
                    std::ifstream ifs(index);
                    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                    crow::response res(content);
                    res.set_header("Content-Type", "text/html; charset=utf-8");
                    return res;
                }
                return crow::response(404, "File not found");
            }
            std::ifstream ifs(full_path, std::ios::binary);
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            crow::response res(content);
            auto ext_pos = file_path.rfind('.');
            if (ext_pos != std::string::npos) {
                std::string ext = file_path.substr(ext_pos + 1);
                if (ext == "html") res.set_header("Content-Type", "text/html; charset=utf-8");
                else if (ext == "css") res.set_header("Content-Type", "text/css; charset=utf-8");
                else if (ext == "js") res.set_header("Content-Type", "application/javascript; charset=utf-8");
                else if (ext == "json") res.set_header("Content-Type", "application/json; charset=utf-8");
                else if (ext == "png") res.set_header("Content-Type", "image/png");
                else if (ext == "jpg" || ext == "jpeg") res.set_header("Content-Type", "image/jpeg");
                else if (ext == "svg") res.set_header("Content-Type", "image/svg+xml");
                else if (ext == "ico") res.set_header("Content-Type", "image/x-icon");
                else if (ext == "woff2") res.set_header("Content-Type", "font/woff2");
                else if (ext == "woff") res.set_header("Content-Type", "font/woff");
                else res.set_header("Content-Type", "application/octet-stream");
            }
            res.set_header("Cache-Control", "public, max-age=31536000");
            return res;
        });
    }

    // Health check + Server info
    CROW_ROUTE(app, "/")
    ([&cfg]() {
        crow::response res("{\"name\":\"MindVault\",\"status\":\"running\",\"version\":\"1.0.0\"}");
        res.set_header("Content-Type", "application/json; charset=utf-8");
        return res;
    });

    // GET /api/server/url - 返回服务器地址
    CROW_ROUTE(app, "/api/server/url").methods("GET"_method)
    ([&cfg]() {
        std::string host_url = "http://localhost:" + std::to_string(cfg.port);
        // 尝试获取局域网 IP
#ifdef _WIN32
        // 通过 icanhazip 或类似服务获取外网 IP 过于复杂
        // 简单返回 host:port，前端会处理 localhost 替换
#endif
        nlohmann::json result = {{"url", host_url}, {"port", cfg.port}};
        crow::response res(utils::Success(result).dump());
        res.set_header("Content-Type", "application/json; charset=utf-8");
        return res;
    });

    std::cout << "[Server] Starting on " << cfg.host << ":" << cfg.port << std::endl;
    std::cout << "[Server] API: http://" << cfg.host << ":" << cfg.port << "/api/" << std::endl;
    app.bindaddr(cfg.host).port(cfg.port).multithreaded().run();

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
