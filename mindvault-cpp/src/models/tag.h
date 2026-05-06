#pragma once
// 标签数据模型

#include <string>
#include <nlohmann/json.hpp>

namespace mindvault::models {

struct Tag {
    int64_t id = 0;
    std::string name;

    static Tag FromJson(const nlohmann::json& row) {
        Tag t;
        t.id   = row.value("id", int64_t(0));
        t.name = row.value("name", std::string(""));
        return t;
    }

    nlohmann::json ToJson() const {
        return {{"id", id}, {"name", name}};
    }
};

} // namespace mindvault::models
