#pragma once
// File routes: /api/files/* + /api/upload/* + /api/tags + /api/drawings

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
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

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

// UTF-8 string → filesystem::path（解决 Windows 中文路径）
inline std::filesystem::path Utf8ToPath(const std::string& s) {
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring ws(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], wlen);
    return std::filesystem::path(ws);
#else
    return std::filesystem::path(s);
#endif
}

// Base64 解码
inline std::vector<unsigned char> Base64Decode(const std::string& encoded) {
    static const int T[] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
    };
    std::vector<unsigned char> result;
    int val = 0, valb = -8;
    for (size_t i = 0; i < encoded.size(); i++) {
        unsigned char c = static_cast<unsigned char>(encoded[i]);
        if (c > 127 || T[c] == -1) continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<unsigned char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

inline void RegisterFileRoutes(crow::App<>& app, Database& db) {
    auto note_svc = std::make_shared<services::NoteService>(db);
    auto tag_svc  = std::make_shared<services::TagService>(db);

    // 确保 uploads 目录存在（用 filesystem::path 保证中文路径正确）
    auto& cfg = utils::Config::Instance();
    std::filesystem::path uploads_dir = cfg.web_dir_fs.parent_path() / "uploads";
    std::filesystem::create_directories(uploads_dir);
    std::cout << "[FileRoutes] Uploads dir: " << uploads_dir.string() << std::endl;

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

    // GET /api/files/export-html/:id - export note as HTML
    CROW_ROUTE(app, "/api/files/export-html/<int>").methods("GET"_method)
    ([note_svc](int64_t id) {
        auto note = note_svc->GetById(id);
        if (note.is_null()) {
            return crow::response(404, utils::NotFound("Note").dump());
        }
        std::string title = note["title"].get<std::string>();
        std::string content = note["content"].get<std::string>();

        std::string html = R"(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>)" + title + R"(</title>
<style>
body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto; max-width: 800px; margin: 0 auto; padding: 40px; background: #1a1b26; color: #a9b1d6; }
h1 { color: #7aa2f7; border-bottom: 1px solid #3b4261; padding-bottom: 10px; }
h2 { color: #bb9af7; } h3 { color: #9ece6a; } h4 { color: #ff9e64; }
code { background: #292e42; padding: 2px 6px; border-radius: 4px; font-family: 'JetBrains Mono', monospace; color: #9ece6a; }
pre { background: #292e42; padding: 16px; border-radius: 8px; overflow-x: auto; border: 1px solid #3b4261; }
pre code { background: transparent; padding: 0; color: #a9b1d6; }
blockquote { border-left: 3px solid #7aa2f7; margin: 14px 0; padding: 6px 18px; color: #565f89; background: rgba(122,162,247,0.1); border-radius: 0 4px 4px 0; }
a { color: #7aa2f7; text-decoration: none; }
table { border-collapse: collapse; width: 100%; margin: 14px 0; }
th, td { border: 1px solid #3b4261; padding: 8px 14px; text-align: left; }
th { background: #292e42; color: #7aa2f7; }
hr { border: none; border-top: 1px solid #3b4261; margin: 24px 0; }
img { max-width: 100%; border-radius: 8px; }
ul, ol { padding-left: 24px; }
li { margin: 4px 0; }
</style></head><body>
<h1>)" + title + "</h1>\n" + content + R"(
</body></html>)";

        crow::response res(html);
        res.set_header("Content-Type", "text/html; charset=utf-8");
        res.set_header("Content-Disposition", "attachment; filename=\"" + title + ".html\"");
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

    // POST /api/upload/image - upload image (multipart)
    CROW_ROUTE(app, "/api/upload/image").methods("POST"_method)
    ([uploads_dir](const crow::request& req) {
        try {
            std::string content_type = req.get_header_value("Content-Type");
            if (content_type.find("multipart/form-data") == std::string::npos) {
                return crow::response(400, utils::Error("Expected multipart/form-data").dump());
            }

            auto boundary_pos = content_type.find("boundary=");
            if (boundary_pos == std::string::npos) {
                return crow::response(400, utils::Error("No boundary found").dump());
            }
            std::string boundary = content_type.substr(boundary_pos + 9);

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

            std::string file_data = body.substr(file_start, file_end - file_start - 2);

            std::string ext = ".png";
            if (content_type.find("jpeg") != std::string::npos || content_type.find("jpg") != std::string::npos) {
                ext = ".jpg";
            } else if (content_type.find("gif") != std::string::npos) {
                ext = ".gif";
            } else if (content_type.find("webp") != std::string::npos) {
                ext = ".webp";
            }

            std::string filename = GenerateRandomId() + ext;
            std::filesystem::path filepath = uploads_dir / filename;

            std::ofstream ofs(filepath, std::ios::binary);
            ofs.write(file_data.data(), file_data.size());
            ofs.close();

            nlohmann::json result = {
                {"url", "/uploads/" + filename},
                {"filename", filename}
            };
            return crow::response(201, utils::Success(result).dump());
        } catch (const std::exception& e) {
            return crow::response(400, utils::Error(e.what()).dump());
        }
    });

    // POST /api/upload/base64 - upload base64 image
    CROW_ROUTE(app, "/api/upload/base64").methods("POST"_method)
    ([uploads_dir](const crow::request& req) {
        try {
            auto body = nlohmann::json::parse(req.body);
            std::string data_url = body.value("image", std::string(""));
            
            if (data_url.empty()) {
                return crow::response(400, utils::Error("image is required").dump());
            }
            
            std::string ext = ".png";
            std::string base64_data;
            
            auto comma_pos = data_url.find(',');
            if (comma_pos != std::string::npos) {
                std::string header = data_url.substr(0, comma_pos);
                base64_data = data_url.substr(comma_pos + 1);
                
                if (header.find("jpeg") != std::string::npos || header.find("jpg") != std::string::npos) {
                    ext = ".jpg";
                } else if (header.find("gif") != std::string::npos) {
                    ext = ".gif";
                } else if (header.find("webp") != std::string::npos) {
                    ext = ".webp";
                } else if (header.find("svg") != std::string::npos) {
                    ext = ".svg";
                }
            } else {
                base64_data = data_url;
            }
            
            std::vector<unsigned char> decoded = Base64Decode(base64_data);
            
            if (decoded.empty()) {
                return crow::response(400, utils::Error("Failed to decode base64 data").dump());
            }

            std::string filename = GenerateRandomId() + ext;
            std::filesystem::path filepath = uploads_dir / filename;
            
            std::ofstream ofs(filepath, std::ios::binary);
            ofs.write(reinterpret_cast<const char*>(decoded.data()), decoded.size());
            ofs.close();
            
            std::cout << "[Upload] Saved: " << filepath.string() << " (" << decoded.size() << " bytes)" << std::endl;
            
            nlohmann::json result = {
                {"url", "/uploads/" + filename},
                {"filename", filename}
            };
            return crow::response(201, utils::Success(result).dump());
        } catch (const std::exception& e) {
            std::cerr << "[Upload] Error: " << e.what() << std::endl;
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
