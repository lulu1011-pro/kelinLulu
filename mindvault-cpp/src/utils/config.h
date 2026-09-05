#pragma once
// MindVault Config Management

#include <string>
#include <filesystem>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace mindvault::utils {

// UTF-8 string → filesystem::path（Windows 中文路径）
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

// filesystem::path → UTF-8 string
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
    std::filesystem::path db_path_fs;
    std::filesystem::path web_dir_fs;
    std::string host = "0.0.0.0";
    int port = 8080;

    static Config& Instance() {
        static Config cfg;
        return cfg;
    }

    void Init(const std::string& base_dir) {
        InitFromPath(Utf8ToPath(base_dir));
    }

    void InitFromPath(const std::filesystem::path& base) {
        db_path_fs = base / "data" / "mindvault.db";
        web_dir_fs = base / "web";
        // 确保 data 目录存在
        std::error_code ec;
        std::filesystem::create_directories(db_path_fs.parent_path(), ec);
        if (ec) {
            std::cerr << "[Config] Warning: cannot create data dir: " << ec.message() << std::endl;
        }
    }

private:
    Config() = default;
};

} // namespace mindvault::utils
