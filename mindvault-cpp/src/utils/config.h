#pragma once
// MindVault Config Management

#include <string>
#include <filesystem>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace mindvault::utils {

// Convert wide path to UTF-8 string (Windows-safe)
inline std::string PathToUtf8(const std::filesystem::path& p) {
#ifdef _WIN32
    auto ws = p.wstring();
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, &result[0], len, nullptr, nullptr);
    return result;
#else
    return p.string();
#endif
}

struct Config {
    std::string db_path;
    std::string web_dir;
    std::string host = "127.0.0.1";
    int port = 8080;

    static Config& Instance() {
        static Config cfg;
        return cfg;
    }

    void Init(const std::string& base_dir) {
        std::filesystem::path base(base_dir);
        db_path = PathToUtf8(base / "data" / "mindvault.db");
        web_dir = PathToUtf8(base / "web");
        // Ensure data directory exists
        std::error_code ec;
        std::filesystem::create_directories(base / "data", ec);
        if (ec) {
            std::cerr << "[Config] Warning: cannot create data dir: " << ec.message() << std::endl;
        }
    }

private:
    Config() = default;
};

} // namespace mindvault::utils
