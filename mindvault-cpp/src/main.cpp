// MindVault -- Local Knowledge Base
// C++ backend entry: start Crow HTTP server

#include "database.h"
#include "routes/note_routes.h"
#include "routes/search_routes.h"
#include "routes/file_routes.h"
#include "routes/ai_routes.h"
#include "utils/config.h"
#include "crow_all.h"
#include <iostream>
#include <filesystem>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace mindvault;

int main() {
    try {
    std::cout << "=== MindVault v1.0.0 ===" << std::endl;
    std::cout << "    Local Knowledge Base" << std::endl;
    std::cout << "========================" << std::endl;

    // Init config
    auto& cfg = utils::Config::Instance();
    char* base_env = std::getenv("MINDVAULT_HOME");
    std::string base_dir;
    if (base_env) {
        base_dir = base_env;
    } else {
        // current_path() returns wide path on Windows; convert to UTF-8
        auto wp = std::filesystem::current_path().wstring();
        int len = WideCharToMultiByte(CP_UTF8, 0, wp.c_str(), -1, nullptr, 0, nullptr, nullptr);
        base_dir.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, wp.c_str(), -1, &base_dir[0], len, nullptr, nullptr);
    }
    cfg.Init(base_dir);
    std::cout << "[Config] Base dir: " << base_dir << std::endl;

    // Init database
    Database db(cfg.db_path);
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
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.code = 204;
        return res;
    });

    // Register routes
    routes::RegisterNoteRoutes(app, db);
    routes::RegisterSearchRoutes(app, db);
    routes::RegisterFileRoutes(app, db);
    routes::RegisterAIRoutes(app, db);

    // Static file serving for uploaded images
    std::string uploads_dir = std::filesystem::path(cfg.web_dir).parent_path().string() + "/uploads";
    CROW_ROUTE(app, "/uploads/<path>")
    ([uploads_dir](const crow::request&, std::string file_path) {
        std::string full_path = uploads_dir + "/" + file_path;
        if (!std::filesystem::exists(full_path)) {
            return crow::response(404, "File not found");
        }
        std::ifstream ifs(full_path, std::ios::binary);
        if (!ifs) {
            return crow::response(500, "Cannot read file");
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        crow::response res(content);

        // 设置 Content-Type
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
    std::string dist_dir = cfg.web_dir + "/dist";
    if (std::filesystem::exists(dist_dir)) {
        std::cout << "[Static] Serving frontend from: " << dist_dir << std::endl;

        // Serve index.html for root
        CROW_ROUTE(app, "/app")
        ([dist_dir]() {
            std::string index_path = dist_dir + "/index.html";
            if (!std::filesystem::exists(index_path)) {
                return crow::response(404, "index.html not found");
            }
            std::ifstream ifs(index_path);
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            crow::response res(content);
            res.set_header("Content-Type", "text/html; charset=utf-8");
            return res;
        });

        // Serve static files
        CROW_ROUTE(app, "/app/<path>")
        ([dist_dir](const crow::request&, std::string file_path) {
            std::string full_path = dist_dir + "/" + file_path;
            if (!std::filesystem::exists(full_path)) {
                // SPA fallback: serve index.html
                std::string index_path = dist_dir + "/index.html";
                if (std::filesystem::exists(index_path)) {
                    std::ifstream ifs(index_path);
                    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                    crow::response res(content);
                    res.set_header("Content-Type", "text/html; charset=utf-8");
                    return res;
                }
                return crow::response(404, "File not found");
            }
            std::ifstream ifs(full_path, std::ios::binary);
            if (!ifs) {
                return crow::response(500, "Cannot read file");
            }
            std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            crow::response res(content);

            // 设置 Content-Type
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

        std::cout << "[Static] Frontend available at: http://" << cfg.host << ":" << cfg.port << "/app" << std::endl;
    }

    // Health check
    CROW_ROUTE(app, "/")([]() {
        nlohmann::json status = {
            {"name", "MindVault"},
            {"version", "1.0.0"},
            {"status", "running"}
        };
        return crow::response(status.dump());
    });

    // Start server
    std::cout << "[Server] Starting on " << cfg.host << ":" << cfg.port << std::endl;
    std::cout << "[Server] API: http://" << cfg.host << ":" << cfg.port << "/api/" << std::endl;
    app.bindaddr(cfg.host).port(cfg.port).multithreaded().run();

    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
