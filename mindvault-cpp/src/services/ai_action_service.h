#pragma once
// P1-6 内容创作套件 + P1-7 自动标签：统一 action 接口
// 结构化 JSON 输出 + 解析容错（重试一次→降级纯文本）
// 短上下文（无历史），复用 CallOnlineAPIWithMessages 的消息格式

#include "../database.h"
#include "tag_service.h"
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>

namespace mindvault::services {

// AI 调用函数类型（路由层传入 CallOnlineAPIWithMessages，service 层不依赖路由层）
using AICaller = std::function<std::string(
    const std::string& api_url, const std::string& api_key,
    const std::string& model, const nlohmann::json& messages)>;

// Action Prompt 模板（五要素：角色、任务、输入、约束、输出格式）
struct ActionPrompt {
    std::string system;
    std::string user_template;  // {text} 占位，translate 额外 {target_lang}
};

class AIActionService {
public:
    AIActionService(Database& db, AICaller caller)
        : db_(db), caller_(std::move(caller)), tag_svc_(db) {}

    // 统一 action 入口：polish|expand|summarize|translate|outline|tags
    // note_id 仅 action=tags 时用（写入 note_tags），其他 action 传 0
    // 返回 {action, result, degraded, warning?, selected_tags?, suggested_new_tags?}
    nlohmann::json ExecuteAction(const std::string& action, const std::string& text,
                                   const std::string& target_lang, int64_t note_id,
                                   const std::string& api_url, const std::string& api_key,
                                   const std::string& model) {
        nlohmann::json result;
        result["action"] = action;
        result["degraded"] = false;

        if (text.empty()) {
            result["result"] = "";
            return result;
        }

        // action=tags 走自动标签专用逻辑
        if (action == "tags") {
            return AutoTag(text, note_id, api_url, api_key, model);
        }

        // 查 Prompt 模板
        auto prompt = GetPrompt(action, target_lang);
        if (prompt.system.empty()) {
            result["result"] = "";
            result["degraded"] = true;
            result["warning"] = "不支持的 action: " + action;
            return result;
        }

        // 构造 messages（短上下文，无历史，只有 system + user 两条）
        std::string user_content = prompt.user_template;
        ReplaceAll(user_content, "{text}", text);
        nlohmann::json messages = {
            {{"role", "system"}, {"content", prompt.system}},
            {{"role", "user"}, {"content", user_content}}
        };

        // 第一次调用 + 提取 JSON
        std::string content = CallAndExtract(api_url, api_key, model, messages);
        nlohmann::json parsed;
        bool ok = TryExtractJson(content, parsed);

        // 解析失败 → 重试一次（更严格的 prompt）
        if (!ok) {
            std::cout << "[AIAction] JSON parse failed, retry with stricter prompt" << std::endl;
            nlohmann::json retry_messages = {
                {{"role", "system"}, {"content", "你是一个严格的JSON输出器。只输出JSON，不要任何解释、说明、代码块围栏。输出必须是合法的JSON对象。"}},
                {{"role", "user"}, {"content", "对以下文本执行" + ActionLabel(action) + "操作，只返回JSON {\"result\": \"...\"}：\n\n" + text}}
            };
            std::string content2 = CallAndExtract(api_url, api_key, model, retry_messages);
            ok = TryExtractJson(content2, parsed);
            if (ok) content = content2;
        }

        // 仍失败 → 降级纯文本
        if (!ok) {
            std::cout << "[AIAction] JSON parse failed again, fallback to plain text" << std::endl;
            result["result"] = content;
            result["degraded"] = true;
            result["warning"] = "模型未返回结构化JSON，已降级为纯文本";
            return result;
        }

        // 解析成功 → 取 result 字段
        result["result"] = parsed.value("result", content);
        return result;
    }

private:
    Database& db_;
    AICaller caller_;
    TagService tag_svc_;

    // 获取 action 对应的 Prompt 模板（五要素：角色、任务、输入、约束、输出格式）
    ActionPrompt GetPrompt(const std::string& action, const std::string& target_lang) {
        if (action == "polish") return {
            "你是一个专业的文字编辑。请润色用户提供的文本，使其更流畅、更专业、更易读。保持原意不变，只优化表达。只返回JSON，不要任何解释。",
            "请润色以下文本，只返回JSON {\"result\": \"润色后的文本\"}：\n\n{text}"
        };
        if (action == "expand") return {
            "你是一个专业的内容创作者。请扩写用户提供的文本，增加细节、例子和解释，使其更丰富。保持原意，不要偏离主题。只返回JSON，不要任何解释。",
            "请扩写以下文本，只返回JSON {\"result\": \"扩写后的文本\"}：\n\n{text}"
        };
        if (action == "summarize") return {
            "你是一个专业的摘要生成器。请总结用户提供的文本，提取核心要点，用简洁的语言概括。只返回JSON，不要任何解释。",
            "请总结以下文本，只返回JSON {\"result\": \"总结内容\"}：\n\n{text}"
        };
        if (action == "translate") {
            std::string lang = target_lang.empty() ? "英文" : target_lang;
            return {
                "你是一个专业的翻译。请将用户提供的文本翻译成目标语言。保持原意和格式。只返回JSON，不要任何解释。",
                "请将以下文本翻译成" + lang + "，只返回JSON {\"result\": \"翻译后的文本\"}：\n\n{text}"
            };
        }
        if (action == "outline") return {
            "你是一个专业的大纲生成器。请根据用户提供的文本生成结构化大纲，用层级列表表示。只返回JSON，不要任何解释。",
            "请为以下文本生成大纲，只返回JSON {\"result\": \"大纲内容\"}：\n\n{text}"
        };
        return {};
    }

    // action 中文标签（重试 prompt 用）
    static std::string ActionLabel(const std::string& action) {
        if (action == "polish") return "润色";
        if (action == "expand") return "扩写";
        if (action == "summarize") return "总结";
        if (action == "translate") return "翻译";
        if (action == "outline") return "生成大纲";
        return action;
    }

    // 调 AI + 从响应提取 choices[0].message.content
    std::string CallAndExtract(const std::string& api_url, const std::string& api_key,
                                const std::string& model, const nlohmann::json& messages) {
        std::string raw = caller_(api_url, api_key, model, messages);
        return ExtractAIContent(raw);
    }

    // 从 API 响应提取 choices[0].message.content
    static std::string ExtractAIContent(const std::string& raw) {
        if (raw.empty()) return "";
        try {
            auto r = nlohmann::json::parse(raw);
            if (r.contains("choices") && r["choices"].size() > 0 &&
                r["choices"][0].contains("message") &&
                r["choices"][0]["message"].contains("content")) {
                return r["choices"][0]["message"]["content"].get<std::string>();
            }
            if (r.contains("error")) {
                return "API 错误: " + r["error"].value("message", "Unknown");
            }
        } catch (...) {}
        return raw;
    }

    // 尝试从模型输出中提取 JSON（去代码块围栏 + 截花括号）
    static bool TryExtractJson(const std::string& content, nlohmann::json& out) {
        if (content.empty()) return false;
        std::string s = content;

        // 去 ```json ... ``` 围栏
        size_t fence_start = s.find("```json");
        if (fence_start != std::string::npos) {
            size_t content_start = fence_start + 7;
            size_t fence_end = s.find("```", content_start);
            if (fence_end != std::string::npos) {
                s = s.substr(content_start, fence_end - content_start);
            }
        } else {
            // 去普通 ``` 围栏
            size_t fs = s.find("```");
            if (fs != std::string::npos) {
                size_t cs = fs + 3;
                size_t fe = s.find("```", cs);
                if (fe != std::string::npos) s = s.substr(cs, fe - cs);
            }
        }

        // 找第一个 { 和最后一个 }
        size_t first = s.find('{');
        size_t last = s.rfind('}');
        if (first == std::string::npos || last == std::string::npos || last <= first) return false;
        std::string json_str = s.substr(first, last - first + 1);
        try {
            out = nlohmann::json::parse(json_str);
            return true;
        } catch (...) {
            return false;
        }
    }

    // P1-7 自动标签：从已有标签集选 3-5 个，建议新标签不入库
    // note_id > 0 时自动写入 note_tags；note_id = 0 时只返回标签名
    nlohmann::json AutoTag(const std::string& text, int64_t note_id,
                            const std::string& api_url, const std::string& api_key,
                            const std::string& model) {
        nlohmann::json result;
        result["action"] = "tags";
        result["degraded"] = false;
        result["selected_tags"] = nlohmann::json::array();
        result["suggested_new_tags"] = nlohmann::json::array();

        // 查已有标签（按使用频率取 top 50，防 prompt 过长）
        auto all_tags = tag_svc_.List();
        std::vector<std::string> tag_names;
        for (auto& t : all_tags) {
            tag_names.push_back(t.value("name", std::string("")));
            if (tag_names.size() >= 50) break;
        }
        if (tag_names.empty()) {
            result["warning"] = "暂无已有标签，请先手动创建一些标签";
            return result;
        }

        std::string tag_list = "[";
        for (size_t i = 0; i < tag_names.size(); ++i) {
            if (i > 0) tag_list += ", ";
            tag_list += "\"" + tag_names[i] + "\"";
        }
        tag_list += "]";

        // 构造 prompt
        std::string system_prompt = "你是一个标签分类器。从给定的已有标签列表中选择3-5个最相关的标签。如果认为需要新标签，单独列在 suggested_new_tags 中。只返回JSON，不要任何解释。";
        std::string user_prompt = "已有标签列表：" + tag_list + "\n\n笔记内容：\n" + text + "\n\n请只返回JSON {\"selected_tags\": [\"标签1\", \"标签2\"], \"suggested_new_tags\": [\"建议的新标签\"]}";

        nlohmann::json messages = {
            {{"role", "system"}, {"content", system_prompt}},
            {{"role", "user"}, {"content", user_prompt}}
        };

        std::string content = CallAndExtract(api_url, api_key, model, messages);
        nlohmann::json parsed;
        bool ok = TryExtractJson(content, parsed);

        // 解析失败 → 重试一次
        if (!ok) {
            std::cout << "[AIAction] AutoTag JSON parse failed, retry" << std::endl;
            nlohmann::json retry_messages = {
                {{"role", "system"}, {"content", "你是一个严格的JSON输出器。只输出JSON，不要任何解释、说明、代码块围栏。"}},
                {{"role", "user"}, {"content", "从标签列表" + tag_list + "中选3-5个标签给以下笔记，只返回JSON {\"selected_tags\": [...], \"suggested_new_tags\": [...]}：\n\n" + text}}
            };
            std::string content2 = CallAndExtract(api_url, api_key, model, retry_messages);
            ok = TryExtractJson(content2, parsed);
        }

        if (!ok) {
            result["degraded"] = true;
            result["warning"] = "模型未返回结构化JSON，自动标签失败";
            return result;
        }

        // 解析 selected_tags：只保留在已有标签集中的，丢弃 AI 发明的新标签
        std::vector<std::string> selected;
        if (parsed.contains("selected_tags") && parsed["selected_tags"].is_array()) {
            for (auto& t : parsed["selected_tags"]) {
                if (!t.is_string()) continue;
                std::string name = t.get<std::string>();
                if (std::find(tag_names.begin(), tag_names.end(), name) != tag_names.end()) {
                    selected.push_back(name);
                }
            }
        }
        result["selected_tags"] = selected;

        // suggested_new_tags：原样返回，绝不入库
        std::vector<std::string> suggested;
        if (parsed.contains("suggested_new_tags") && parsed["suggested_new_tags"].is_array()) {
            for (auto& t : parsed["suggested_new_tags"]) {
                if (t.is_string()) suggested.push_back(t.get<std::string>());
            }
        }
        result["suggested_new_tags"] = suggested;

        // note_id > 0 时写入 note_tags
        if (note_id > 0 && !selected.empty()) {
            for (const auto& name : selected) {
                tag_svc_.AddTagToNote(note_id, name);
            }
            std::cout << "[AIAction] AutoTag wrote " << selected.size() << " tags to note " << note_id << std::endl;
        }

        return result;
    }

    // 字符串替换
    static void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
        if (from.empty()) return;
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.length(), to);
            pos += to.length();
        }
    }
};

} // namespace mindvault::services
