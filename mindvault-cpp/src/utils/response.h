#pragma once
// MindVault 统一 JSON 响应格式

#include <nlohmann/json.hpp>
#include <string>
#include "crow_all.h"

namespace mindvault::utils {

inline nlohmann::json Success(const nlohmann::json& data = nullptr) {
    nlohmann::json res;
    res["ok"] = true;
    res["data"] = data;
    return res;
}

inline nlohmann::json Error(const std::string& msg, int code = 400) {
    nlohmann::json res;
    res["ok"] = false;
    res["error"] = {{"code", code}, {"message", msg}};
    return res;
}

inline nlohmann::json NotFound(const std::string& resource) {
    return Error(resource + " not found", 404);
}

// 创建带正确 Content-Type 的响应
inline crow::response JsonResp(const nlohmann::json& data, int code = 200) {
    crow::response res(code, data.dump());
    res.set_header("Content-Type", "application/json; charset=utf-8");
    return res;
}

} // namespace mindvault::utils
