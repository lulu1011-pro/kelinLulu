#pragma once
// File routes: /api/files/* + /api/upload/* + static files

#include "../database.h"
#include "../services/note_service.h"
#include "../services/tag_service.h"
#include "../utils/response.h"
#include "../utils/config.h"
#include "crow_all.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <memory>
#include <filesystem>
#include <random>
#include <iomanip>

namespace mindvault::routes {

inline std::string GenerateRandomId(int length = 8) {
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(chars) - 2);
    std::string result;
    for (int i = 0; i < length; ++i) result += chars[dist(gen)];
    return result;
}

inline void RegisterFileRoutes(crow::App<>& app, Database& db) {
    auto note_svc = std::make_shared<services::NoteService>(db);
    auto tag_svc  = std::make_shared<services::TagService>(db);

    // 确保 uploads 目录存在
    auto& cfg = utils::Config::Instance();
    std::filesystem::path uploads_dir = std::filesystem::path(cfg.web_dir).parent_path() / "uploads";
    std::filesystem::create_directories(uploads_dir);
    std::string uploads_path = uploads_dir.string();

    // GET /api/files/export/:id - export note as .md
    CROW_ROUTE(app, "/api/files/export/<int>").methods("GET"_method)
    ([note_svc](int64_t id) {
        auto note = note_svc->GetById(id);
        if (note.is_null()) {
            return crow::response(404, utils::NotFound("Note").dump());
        }
        std::string md = "# " + note["title"].get<std::string>() + "\n\n" + note["content"].get<std::string>();
        crow::response res(md);
        res.set_header("Content-Type", "text/markdown; charset=utf-8");
        res.set_header("Content-Disposition",
            "attachment; filename=\"" + note["title"].get<std::string>() + ".md\"");
        return res;
    });

    // POST /api/files/import - import .md file
    CROW_ROUTE(app, "/api/files/import").methods("POST"_method)
    ([note_svc](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string filename = body.value("filename", std::string("Untitled"));
            std::string content  = body.value("content", std::string(""));
            std::string folder   = body.value("folder", std::string("imported"));

            // Auto-extract # title from first line
            if (content.size() >= 2 && content.substr(0, 2) == "# ") {
                auto nl = content.find('\n');
                if (nl != std::string::npos) {
                    filename = content.substr(2, nl - 2);
                    content = content.substr(nl + 1);
                    while (!content.empty() && content[0] == '\n') content.erase(0, 1);
                }
            }

            auto note = note_svc->Create(filename, content, folder);
            return crow::response(201, utils::Success(note).dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // POST /api/upload/image - upload image
    CROW_ROUTE(app, "/api/upload/image").methods("POST"_method)
    ([uploads_path](const crow::request& req) {
        try {
            // 解析 multipart form data
            std::string content_type = req.get_header_value("Content-Type");
            if (content_type.find("multipart/form-data") == std::string::npos) {
                return crow::response(400, utils::Error("Expected multipart/form-data").dump());
            }

            // 提取 boundary
            auto boundary_pos = content_type.find("boundary=");
            if (boundary_pos == std::string::npos) {
                return crow::response(400, utils::Error("No boundary found").dump());
            }
            std::string boundary = content_type.substr(boundary_pos + 9);

            // 简单解析：查找文件内容
            std::string body = req.body;
            auto file_start = body.find("\r\n\r\n");
            if (file_start == std::string::npos) {
                return crow::response(400, utils::Error("Invalid form data").dump());
            }
            file_start += 4;

            auto file_end = body.find("--" + boundary, file_start);
            if (file_end == std::string::npos) {
                return crow::response(400, utils::Error("Invalid form data").dump());
            }

            std::string file_data = body.substr(file_start, file_end - file_start - 2); // -2 for \r\n

            // 生成文件名
            std::string ext = ".png";
            if (content_type.find("jpeg") != std::string::npos || content_type.find("jpg") != std::string::npos) {
                ext = ".jpg";
            } else if (content_type.find("gif") != std::string::npos) {
                ext = ".gif";
            } else if (content_type.find("webp") != std::string::npos) {
                ext = ".webp";
            }

            std::string filename = GenerateRandomId() + ext;
            std::string filepath = uploads_path + "/" + filename;

            // 写入文件
            std::ofstream ofs(filepath, std::ios::binary);
            ofs.write(file_data.data(), file_data.size());
            ofs.close();

            // 返回 URL
            nlohmann::json result = {
                {"url", "/uploads/" + filename},
                {"filename", filename}
            };
            return crow::response(201, utils::Success(result).dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // GET /api/tags - list all tags
    CROW_ROUTE(app, "/api/tags").methods("GET"_method)
    ([tag_svc]() {
        auto tags = tag_svc->List();
        return crow::response(utils::Success(tags).dump());
    });
}

} // namespace mindvault::routes
