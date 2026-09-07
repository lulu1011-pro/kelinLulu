#pragma once
// P2-2 function calling 轻量用法：只做 2 个真实只读工具
// 工具：search_notes（对应 SearchService::Search）、get_note（对应 NoteService::GetById）
// 单轮 tool loop：第一次调AI(带tools) → 执行工具 → 第二次调AI(不带tools)
// 三层降级：无tool_calls直接返回 / 解析失败降级 / 整体异常降级

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

// 带 tools 的 AI 调用函数类型（tools 为空时等价于普通调用）
using FCAICaller = std::function<std::string(
    const std::string& api_url, const std::string& api_key,
    const std::string& model, const nlohmann::json& messages,
    const nlohmann::json& tools)>;

class FunctionCallingService {
public:
    FunctionCallingService(Database& db, FCAICaller caller)
        : db_(db), caller_(std::move(caller)), note_svc_(db), search_svc_(db) {}

    // 主入口：单轮 tool loop
    // 返回 {answer, tool_calls: [{name, args, result, status}], degraded?, warning?}
    nlohmann::json ChatWithTools(const std::string& question,
                                  const std::string& api_url, const std::string& api_key,
                                  const std::string& model) {
        nlohmann::json result;
        result["tool_calls"] = nlohmann::json::array();
        result["degraded"] = false;

        if (question.empty()) {
            result["answer"] = "";
            result["warning"] = "question is required";
            return result;
        }

        try {
            // 第一步：第一次调 AI（带 tools 定义）
            nlohmann::json messages = {
                {{"role", "system"}, {"content", "你是一个知识库助手。可以使用搜索笔记和获取笔记内容的工具来帮助回答用户问题。如果需要查找资料，请调用工具。"}},
                {{"role", "user"}, {"content", question}}
            };

            std::string raw1 = caller_(api_url, api_key, model, messages, GetToolDefinitions());
            nlohmann::json resp1 = nlohmann::json::parse(raw1);
            nlohmann::json msg1 = resp1["choices"][0]["message"];

            // 情况 B：AI 直接回答，没有 tool_calls → 直接返回
            if (!msg1.contains("tool_calls") || msg1["tool_calls"].is_null() || msg1["tool_calls"].empty()) {
                result["answer"] = msg1.value("content", std::string(""));
                return result;
            }

            // 情况 A：有 tool_calls → 执行工具
            nlohmann::json tool_calls = msg1["tool_calls"];
            nlohmann::json executed_calls = nlohmann::json::array();

            // 把第一次的 message 加入历史（含 tool_calls）
            nlohmann::json messages2 = messages;
            messages2.push_back(msg1);

            for (const auto& tc : tool_calls) {
                std::string tool_name = tc["function"]["name"].get<std::string>();
                std::string args_str = tc["function"].value("arguments", std::string("{}"));
                std::string tool_id = tc.value("id", std::string("call_0"));

                nlohmann::json call_record;
                call_record["name"] = tool_name;
                call_record["args"] = args_str;

                // 执行工具
                std::string tool_result_str;
                std::string status = "success";
                try {
                    nlohmann::json args = nlohmann::json::parse(args_str);
                    nlohmann::json tool_result = ExecuteTool(tool_name, args);
                    tool_result_str = tool_result.dump();
                } catch (const std::exception& e) {
                    tool_result_str = std::string("{\"error\": \"工具执行失败: ") + e.what() + "\"}";
                    status = "error";
                }

                call_record["result"] = tool_result_str;
                call_record["status"] = status;
                executed_calls.push_back(call_record);

                // 把工具结果作为 tool role 消息加入历史
                messages2.push_back({
                    {"role", "tool"},
                    {"tool_call_id", tool_id},
                    {"content", tool_result_str}
                });
            }

            result["tool_calls"] = executed_calls;

            // 第三步：第二次调 AI（不带 tools，让 AI 基于工具结果生成最终回答）
            std::string raw2 = caller_(api_url, api_key, model, messages2, nlohmann::json::array());
            nlohmann::json resp2 = nlohmann::json::parse(raw2);
            std::string answer = resp2["choices"][0]["message"]["content"].get<std::string>();
            result["answer"] = answer;

        } catch (const std::exception& e) {
            std::cout << "[FunctionCalling] error: " << e.what() << std::endl;
            // 整体异常降级：返回错误提示
            result["answer"] = "工具调用模式出错，已降级为普通模式。请重试或关闭工具模式。";
            result["degraded"] = true;
            result["warning"] = std::string("function calling 异常: ") + e.what();
        }

        return result;
    }

private:
    Database& db_;
    FCAICaller caller_;
    NoteService note_svc_;
    SearchService search_svc_;

    // ─── 工具定义（OpenAI 兼容格式）───
    nlohmann::json GetToolDefinitions() {
        return nlohmann::json::array({
            {
                {"type", "function"},
                {"function", {
                    {"name", "search_notes"},
                    {"description", "按关键词搜索笔记，返回匹配的笔记列表（含标题和内容摘要）"},
                    {"parameters", {
                        {"type", "object"},
                        {"properties", {
                            {"query", {{"type", "string"}, {"description", "搜索关键词"}}}
                        }},
                        {"required", nlohmann::json::array({"query"})}
                    }}
                }}
            },
            {
                {"type", "function"},
                {"function", {
                    {"name", "get_note"},
                    {"description", "根据笔记ID获取笔记完整内容"},
                    {"parameters", {
                        {"type", "object"},
                        {"properties", {
                            {"note_id", {{"type", "integer"}, {"description", "笔记ID"}}}
                        }},
                        {"required", nlohmann::json::array({"note_id"})}
                    }}
                }}
            }
        });
    }

    // ─── 执行工具：和代码函数一一对应 ───
    nlohmann::json ExecuteTool(const std::string& name, const nlohmann::json& args) {
        if (name == "search_notes") {
            std::string query = args.value("query", std::string(""));
            if (query.empty()) {
                return nlohmann::json::array();
            }
            // 对应 SearchService::Search，结果截断最多 5 条，每条保留关键字段
            auto raw = search_svc_.Search(query, 5);
            nlohmann::json truncated = nlohmann::json::array();
            for (const auto& r : raw) {
                nlohmann::json item;
                item["id"] = r.value("id", (int64_t)0);
                item["title"] = r.value("title", std::string(""));
                std::string snippet = r.value("content_highlight", std::string(""));
                // 工具模式的 snippet 给 600 字（普通模式的 3 倍），让 AI 一次看到足够多的内容
                if (snippet.length() > 600) snippet = snippet.substr(0, 600) + "...";
                item["snippet"] = snippet;
                truncated.push_back(item);
            }
            return truncated;
        }

        if (name == "get_note") {
            int64_t note_id = args.value("note_id", (int64_t)0);
            if (note_id <= 0) {
                return nlohmann::json{{"error", "note_id must be positive"}};
            }
            // 对应 NoteService::GetById
            auto note = note_svc_.GetById(note_id);
            if (note.is_null()) {
                return nlohmann::json{{"error", "笔记不存在"}};
            }
            nlohmann::json result;
            result["id"] = note.value("id", (int64_t)0);
            result["title"] = note.value("title", std::string(""));
            std::string content = note.value("content", std::string(""));
            // 工具模式：3000 字上限（约 2000 token），给 AI 足够上下文（普通模式 RAG 注入了 1200 字/篇）
            if (content.length() > 3000) content = content.substr(0, 3000) + "\n\n... (后续内容省略，请让 AI 用 search_notes 工具检索其他笔记，或分批询问)";
            result["content"] = content;
            return result;
        }

        // 未知工具
        return nlohmann::json{{"error", "未知工具: " + name}};
    }
};

} // namespace mindvault::services
