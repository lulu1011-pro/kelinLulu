#pragma once
// 笔记数据模型

#include <string>
#include <nlohmann/json.hpp>

namespace mindvault::models {

struct Note {
    int64_t id = 0;
    std::string title;
    std::string content;
    std::string folder = "default";
    int is_deleted = 0;
    std::string created_at;
    std::string updated_at;

    // 从 JSON 行构造
    static Note FromJson(const nlohmann::json& row) {
        Note n;
        n.id         = row.value("id", int64_t(0));
        n.title      = row.value("title", std::string(""));
        n.content    = row.value("content", std::string(""));
        n.folder     = row.value("folder", std::string("default"));
        n.is_deleted = row.value("is_deleted", 0);
        n.created_at = row.value("created_at", std::string(""));
        n.updated_at = row.value("updated_at", std::string(""));
        return n;
    }

    // 转为 JSON（列表模式，不含 content）
    nlohmann::json ToListJson() const {
        return {
            {"id", id}, {"title", title}, {"folder", folder},
            {"is_deleted", is_deleted},
            {"created_at", created_at}, {"updated_at", updated_at}
        };
    }

    // 转为 JSON（详情模式，含 content）
    nlohmann::json ToDetailJson() const {
        auto j = ToListJson();
        j["content"] = content;
        return j;
    }
};

} // namespace mindvault::models
