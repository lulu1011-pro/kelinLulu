#pragma once
// P2-1 知识图谱问答：结构化问题走 SQL，非结构化问题走 LLM
// 基于现有 link_edges 图，不引图数据库
// 路由判定用关键词正则（非结构化优先级高），不调 AI 做路由

#include "../database.h"
#include "note_service.h"
#include "search_service.h"
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>

namespace mindvault::services {

// 复用 AICaller 类型（和 ai_action_service.h 一致）
using GraphAICaller = std::function<std::string(
    const std::string& api_url, const std::string& api_key,
    const std::string& model, const nlohmann::json& messages)>;

class GraphQAService {
public:
    GraphQAService(Database& db, GraphAICaller caller)
        : db_(db), caller_(std::move(caller)), note_svc_(db), search_svc_(db) {}

    // 统一入口：根据问题自动路由
    // 返回 {route: "structured"|"unstructured", notes?: [...], answer?: "...", sources?: [...]}
    nlohmann::json Answer(const std::string& question, int64_t context_note_id,
                           const std::string& api_url, const std::string& api_key,
                           const std::string& model) {
        nlohmann::json result;

        if (question.empty()) {
            result["route"] = "error";
            result["error"] = "question is required";
            return result;
        }

        // 第一步：路由判定
        std::string route = RouteQuestion(question);
        result["route"] = route;

        // 第二步：从问题中提取笔记名（如果有上下文 note_id 就直接用）
        int64_t note_id = context_note_id;
        std::string note_name;
        if (note_id <= 0) {
            note_name = ExtractNoteName(question);
            if (!note_name.empty()) {
                note_id = FindNoteIdByName(note_name);
            }
        }

        if (route == "structured") {
            return AnswerStructured(question, note_id, note_name, result);
        } else {
            return AnswerUnstructured(question, note_id, note_name, api_url, api_key, model, result);
        }
    }

private:
    Database& db_;
    GraphAICaller caller_;
    NoteService note_svc_;
    SearchService search_svc_;

    // ─── 路由判定：关键词正则，非结构化优先级高 ───
    // 非结构化关键词（含这些就走 LLM）：总结/解释/为什么/对比/讲了什么/区别/关系/分析
    // 结构化关键词（含这些且不含非结构化词）：哪些/谁/几个/引用了/被引用/链接/关联/列出
    std::string RouteQuestion(const std::string& q) {
        static const std::vector<std::string> unstructured_keywords = {
            "总结", "解释", "为什么", "对比", "讲了什么", "区别", "关系", "分析",
            "概括", "描述", "说明", "怎么样", "如何", "评价"
        };
        static const std::vector<std::string> structured_keywords = {
            "哪些", "谁", "几个", "引用了", "被引用", "链接", "关联", "列出",
            "有哪些", "都有谁", "数量"
        };

        // 非结构化优先级高：同时命中时走 LLM（用户要的是"总结"而非"列表"）
        for (const auto& kw : unstructured_keywords) {
            if (q.find(kw) != std::string::npos) return "unstructured";
        }
        for (const auto& kw : structured_keywords) {
            if (q.find(kw) != std::string::npos) return "structured";
        }
        // 默认走非结构化（更安全，LLM 能处理模糊问题）
        return "unstructured";
    }

    // ─── 从问题中提取笔记名 ───
    // 简单策略：去掉关键词后，取剩余的核心短语；实际用 FTS5 搜索匹配
    std::string ExtractNoteName(const std::string& q) {
        // 去掉常见疑问词和结构化关键词，剩下的可能是笔记名
        std::string cleaned = q;
        static const std::vector<std::string> remove_words = {
            "哪些笔记", "哪些", "笔记", "引用了", "被引用", "链接到", "链接",
            "关联", "列出", "有哪些", "都有谁", "几个", "谁", "总结", "解释",
            "为什么", "对比", "讲了什么", "区别", "关系", "分析", "的", "了",
            "吗", "呢", "？", "?", " ", "　"
        };
        for (const auto& w : remove_words) {
            size_t pos;
            while ((pos = cleaned.find(w)) != std::string::npos) {
                cleaned.replace(pos, w.length(), " ");
            }
        }
        //  trim
        while (!cleaned.empty() && (cleaned.front() == ' ' || cleaned.front() == '　')) cleaned.erase(0, 1);
        while (!cleaned.empty() && (cleaned.back() == ' ' || cleaned.back() == '　')) cleaned.pop_back();
        return cleaned;
    }

    // ─── 用 FTS5 模糊匹配找 note_id，找不到返回 0 ───
    int64_t FindNoteIdByName(const std::string& name) {
        if (name.empty()) return 0;
        try {
            // 先试 FTS5 全文搜索
            auto results = search_svc_.Search(name, 5);
            if (!results.empty() && results[0].contains("id")) {
                return results[0]["id"].get<int64_t>();
            }
            // FTS5 失败降级为标题精确匹配
            auto by_title = search_svc_.SearchByTitle(name, 5);
            if (!by_title.empty() && by_title[0].contains("id")) {
                return by_title[0]["id"].get<int64_t>();
            }
        } catch (const std::exception& e) {
            std::cout << "[GraphQA] FindNoteIdByName error: " << e.what() << std::endl;
        }
        return 0;
    }

    // ─── 结构化路：纯 SQL，不调 AI ───
    nlohmann::json AnswerStructured(const std::string& question, int64_t note_id,
                                     const std::string& note_name, nlohmann::json result) {
        if (note_id <= 0) {
            result["notes"] = nlohmann::json::array();
            result["warning"] = note_name.empty() ?
                "未在问题中识别到笔记名" : "未找到笔记「" + note_name + "」";
            return result;
        }

        // 判断方向：含"被引用/引用了我/谁链接到"→反向（谁链接到我）；否则正向（我链接到谁）
        bool is_backward = (question.find("被引用") != std::string::npos ||
                           question.find("引用了我") != std::string::npos ||
                           question.find("谁链接到") != std::string::npos ||
                           question.find("哪些笔记引用") != std::string::npos);

        nlohmann::json links = is_backward ? note_svc_.GetBacklinks(note_id)
                                            : note_svc_.GetForwardLinks(note_id);
        result["notes"] = links;
        result["direction"] = is_backward ? "backward" : "forward";
        result["note_id"] = note_id;
        if (links.empty()) {
            result["warning"] = "该笔记暂无关联链接";
        }
        return result;
    }

    // ─── 非结构化路：图 + LLM 结合 ───
    nlohmann::json AnswerUnstructured(const std::string& question, int64_t note_id,
                                       const std::string& note_name,
                                       const std::string& api_url, const std::string& api_key,
                                       const std::string& model, nlohmann::json result) {
        // 先查 link_edges 获取关联笔记（正向+反向合并去重）
        nlohmann::json related_notes = nlohmann::json::array();
        std::unordered_map<int64_t, bool> seen;
        if (note_id > 0) {
            auto fwd = note_svc_.GetForwardLinks(note_id);
            auto bwd = note_svc_.GetBacklinks(note_id);
            for (const auto& n : fwd) {
                int64_t id = n.value("id", (int64_t)0);
                if (id > 0 && !seen[id]) { seen[id] = true; related_notes.push_back(n); }
            }
            for (const auto& n : bwd) {
                int64_t id = n.value("id", (int64_t)0);
                if (id > 0 && !seen[id]) { seen[id] = true; related_notes.push_back(n); }
            }
        }

        // 如果没有关联笔记，降级为结构化结果
        if (related_notes.empty()) {
            result["notes"] = related_notes;
            result["answer"] = note_id > 0 ?
                "该笔记暂无关联链接，无法基于图谱生成回答。" :
                "未在问题中识别到笔记名，请指定具体笔记。";
            result["degraded"] = true;
            return result;
        }

        // 取关联笔记的标题 + 前200字作为上下文
        std::string context;
        nlohmann::json sources = nlohmann::json::array();
        for (size_t i = 0; i < related_notes.size() && i < 10; i++) {
            int64_t rid = related_notes[i].value("id", (int64_t)0);
            std::string title = related_notes[i].value("title", std::string(""));
            auto note = note_svc_.GetById(rid);
            std::string content = note.is_null() ? "" : note.value("content", std::string(""));
            if (content.length() > 200) content = content.substr(0, 200) + "...";
            context += "【" + title + "】\n" + content + "\n\n";
            sources.push_back({{"id", rid}, {"title", title}});
        }

        // 拼 prompt 调 AI
        std::string system_prompt = "你是一个知识库问答助手。请根据用户提供的关联笔记内容，回答用户关于笔记之间关系的问题。回答要简洁准确，引用笔记时注明笔记标题。";
        std::string user_prompt = "关联笔记内容：\n" + context + "\n用户问题：" + question;

        nlohmann::json messages = {
            {{"role", "system"}, {"content", system_prompt}},
            {{"role", "user"}, {"content", user_prompt}}
        };

        try {
            std::string ai_response = caller_(api_url, api_key, model, messages);
            // 解析 AI 返回的 JSON（CallOnlineAPIWithMessages 返回完整 JSON）
            nlohmann::json parsed = nlohmann::json::parse(ai_response);
            std::string answer = parsed["choices"][0]["message"]["content"].get<std::string>();
            result["answer"] = answer;
            result["sources"] = sources;
        } catch (const std::exception& e) {
            std::cout << "[GraphQA] AI call failed: " << e.what() << std::endl;
            // AI 调用失败，降级为返回关联笔记列表
            result["answer"] = "AI 回答生成失败，以下是关联笔记列表：";
            result["sources"] = sources;
            result["notes"] = related_notes;
            result["degraded"] = true;
            result["warning"] = "AI 调用失败，已降级为纯结构化结果";
        }

        return result;
    }
};

} // namespace mindvault::services
