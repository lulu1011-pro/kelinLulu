#pragma once
// MindVault 统一 JSON 响应格式

#include <nlohmann/json.hpp>
#include <string>

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

} // namespace mindvault::utils
