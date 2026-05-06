# MindVault AI集成架构文档

## 文档信息

| 字段 | 内容 |
|-----|------|
| 项目名称 | MindVault 本地知识库 |
| 文档版本 | v2.0 |
| 创建日期 | 2026-04-29 |
| 更新日期 | 2026-05-04 |
| 状态 | 设计文档（待实现） |

---

## 一、AI功能架构

### 1.1 AI能力分层

```
┌─────────────────────────────────────────────────────────┐
│                  用户交互层                               │
│  对话面板 | 内容创作 | 知识梳理 | 辅助工具              │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│                  AI调度层                                │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐          │
│  │ 任务路由  │  │ 提示词管理│  │ 结果解析 │          │
│  └──────────┘  └──────────┘  └──────────┘          │
└────────────────────┬────────────────────────────────────┘
                     │
        ┌────────────┼────────────┐
        ▼            ▼            ▼
┌───────────┐ ┌───────────┐ ┌───────────┐
│  本地LLM  │ │  在线API  │ │  混合模式  │
│ (Ollama)  │ │(OpenAI等)│ │           │
└───────────┘ └───────────┘ └───────────┘
        │            │            │
        └────────────┼────────────┘
                     ▼
┌─────────────────────────────────────────────────────────┐
│                  RAG增强层                                │
│  向量检索 | 上下文压缩 | 引用溯源 | 多轮对话            │
└─────────────────────────────────────────────────────────┘
```

### 1.2 当前架构（C++后端 + Vue前端）

```
Vue3前端
    │
    ├─ AIChatPanel.vue (待实现)
    │   └─ 用户输入 → 调用后端AI API
    │
    ↓ HTTP请求
C++后端 (Crow)
    │
    ├─ /api/ai/chat (待实现)
    │   └─ AIChatService → 调用Ollama/在线API
    │
    ├─ /api/ai/embed (待实现)
    │   └─ 生成文本向量嵌入
    │
    └─ /api/search (已实现)
        └─ FTS5全文检索 → RAG检索增强
```

### 1.3 Ollama集成方案（C++实现）

```cpp
// src/services/ai_service.h (待实现)
#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace mindvault::services {

class AIService {
public:
    AIService(const std::string& ollama_url = "http://localhost:11434");

    // 检查Ollama是否可用
    bool isAvailable();

    // 获取可用模型列表
    std::vector<std::string> getModels();

    // 生成回答（非流式）
    std::string generate(const std::string& model, const std::string& prompt);

    // 流式生成（用于对话）
    // 需要WebSocket或SSE支持

    // 嵌入向量（用于RAG）
    std::vector<float> embed(const std::string& text, const std::string& model = "nomic-embed-text");

private:
    std::string ollama_url_;

    // HTTP客户端（调用Ollama API）
    nlohmann::json httpPost(const std::string& endpoint, const nlohmann::json& body);
};

} // namespace mindvault::services
```

### 1.4 RAG架构（检索增强生成）

```cpp
// src/services/rag_service.h (待实现)
#pragma once
#include "ai_service.h"
#include "search_service.h"
#include "note_service.h"

namespace mindvault::services {

class RAGService {
public:
    RAGService(AIService& ai, SearchService& search, NoteService& notes);

    // RAG问答
    struct RAGResponse {
        std::string answer;
        std::vector<nlohmann::json> sources;  // 引用来源
    };

    RAGResponse query(const std::string& question, int max_results = 5);

private:
    AIService& ai_;
    SearchService& search_;
    NoteService& notes_;

    // 检索相关知识
    std::vector<nlohmann::json> retrieveRelevantDocs(const std::string& query, int max_results);

    // 构建RAG提示词
    std::string buildRAGPrompt(const std::string& question, const std::vector<nlohmann::json>& docs);

    // 提取相关片段
    std::string extractSnippet(const std::string& content, const std::string& query, int context_len = 100);
};

} // namespace mindvault::services
```

### 1.5 RAG提示词模板

```cpp
// RAG提示词构建
std::string RAGService::buildRAGPrompt(const std::string& question, const std::vector<nlohmann::json>& docs) {
    std::string context;
    for (size_t i = 0; i < docs.size(); ++i) {
        context += "[来源" + std::to_string(i+1) + ": " + docs[i]["title"].get<std::string>() + "]\n";
        context += docs[i]["snippet"].get<std::string>() + "\n\n";
    }

    return R"(你是一个知识库助手。请基于以下参考内容回答问题。
如果参考资料无法回答，请明确说明，不要编造信息。

## 参考资料：
)" + context + R"(

## 问题：
)" + question + R"(

## 回答：)";
}
```

---

## 二、AI功能模块设计

### 2.1 内容创作

```cpp
// 提示词模板
namespace prompts {

std::string outline(const std::string& topic) {
    return "请为主题\"" + topic + "\"生成一个详细的文章大纲，包含：\n"
           "1. 引言\n2. 主体部分（至少3个主要章节）\n3. 结论\n"
           "要求：结构清晰，逻辑严密。";
}

std::string expand(const std::string& text) {
    return "请将以下内容扩写，增加细节和例子：\n\"" + text + "\"\n"
           "要求：保持原意，语言流畅，内容充实。";
}

std::string summarize(const std::string& text) {
    return "请将以下内容总结为3-5个要点的摘要：\n\"" + text + "\"";
}

std::string polish(const std::string& text, const std::string& style) {
    return "请将以下文字润色为" + style + "风格：\n\"" + text + "\"";
}

std::string translate(const std::string& text, const std::string& target_lang) {
    return "请将以下文字翻译为" + target_lang + "：\n\"" + text + "\"";
}

} // namespace prompts
```

### 2.2 知识梳理

```cpp
// 自动标签推荐
std::vector<std::string> recommendTags(const std::string& content, const std::vector<std::string>& existing_tags) {
    std::string prompt = "分析以下笔记内容，从已有标签中选择最相关的3-5个标签。\n"
                        "如果都不合适，可以推荐新标签（以\"新:\"开头）。\n\n"
                        "已有标签：";
    for (const auto& tag : existing_tags) {
        prompt += tag + ", ";
    }
    prompt += "\n\n笔记内容：\n" + content.substr(0, 1000) + "...\n\n"
              "请返回JSON数组格式，例如：[\"标签1\", \"标签2\", \"新:新标签3\"]";

    std::string response = ai_.generate("qwen2.5:7b", prompt);
    // 解析JSON响应
    try {
        auto json = nlohmann::json::parse(response);
        return json.get<std::vector<std::string>>();
    } catch (...) {
        return {};
    }
}
```

### 2.3 侧边栏AI对话（前端）

```vue
<!-- AIChatPanel.vue (待实现) -->
<script setup lang="ts">
import { ref } from 'vue'

interface ChatMessage {
    id: string
    role: 'user' | 'assistant'
    content: string
    sources?: { noteId: number; title: string; snippet: string }[]
    timestamp: number
}

const messages = ref<ChatMessage[]>([])
const input = ref('')
const loading = ref(false)

async function sendMessage() {
    if (!input.value.trim() || loading.value) return

    // 添加用户消息
    messages.value.push({
        id: Date.now().toString(),
        role: 'user',
        content: input.value,
        timestamp: Date.now()
    })

    const question = input.value
    input.value = ''
    loading.value = true

    try {
        // 调用后端RAG API
        const res = await fetch('/api/ai/chat', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ question })
        })
        const data = await res.json()

        // 添加AI回复
        messages.value.push({
            id: (Date.now() + 1).toString(),
            role: 'assistant',
            content: data.answer,
            sources: data.sources,
            timestamp: Date.now()
        })
    } catch (e) {
        console.error('AI chat failed:', e)
    } finally {
        loading.value = false
    }
}
</script>
```

---

## 三、AI模型兼容列表

### 3.1 本地模型（Ollama）

| 模型 | 大小 | 用途 | 推荐场景 |
|-----|------|------|---------|
| **Qwen2.5:7b** | 4.7GB | 通用对话 | 首选，中文友好 |
| **Llama 3.2:3b** | 2.0GB | 轻量对话 | 低配置机器 |
| **Phi-3:mini** | 2.3GB | 快速推理 | 简单任务 |
| **nomic-embed-text** | 274MB | 向量嵌入 | RAG必备 |

### 3.2 在线API

| 服务商 | 模型 | 特点 | 适用场景 |
|-------|-----|------|---------|
| OpenAI | gpt-4o | 能力强 | 复杂推理、创作 |
| OpenAI | gpt-4o-mini | 快+便宜 | 日常对话 |
| 通义千问 | qwen-max | 中文强 | 中文内容处理 |
| 豆包 | doubao-pro | 中文优 | 中文润色 |

---

## 四、AI配置管理

```cpp
// src/utils/ai_config.h (待实现)
#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace mindvault::utils {

struct AIConfig {
    // 模式选择
    enum class Mode { LOCAL, API, HYBRID };
    Mode mode = Mode::LOCAL;

    // 本地LLM设置
    struct Local {
        bool enabled = true;
        std::string base_url = "http://localhost:11434";
        std::string model = "qwen2.5:7b";
        std::string embedding_model = "nomic-embed-text";
    } local;

    // API设置
    struct API {
        std::string provider = "openai";  // openai, tongyi, doubao
        std::string api_key = "";
        std::string base_url = "";
        std::string model = "gpt-4o-mini";
    } api;

    // 从JSON加载
    static AIConfig fromJson(const nlohmann::json& j) {
        AIConfig cfg;
        // ... 解析配置
        return cfg;
    }

    // 保存为JSON
    nlohmann::json toJson() const {
        return {
            {"mode", static_cast<int>(mode)},
            {"local", {{"enabled", local.enabled}, {"base_url", local.base_url}, {"model", local.model}}},
            {"api", {{"provider", api.provider}, {"model", api.model}}}
        };
    }
};

} // namespace mindvault::utils
```

---

## 五、实现路线图

### Phase 1: 基础AI集成（4周）

| 周次 | 任务 | 交付物 |
|-----|------|-------|
| Week 1 | Ollama HTTP客户端封装 | AIService类 |
| Week 2 | RAG检索服务（基于FTS5） | RAGService类 |
| Week 3 | AI对话API端点 | /api/ai/chat |
| Week 4 | 前端AI对话面板 | AIChatPanel.vue |

### Phase 2: AI功能增强（4周）

| 周次 | 任务 | 交付物 |
|-----|------|-------|
| Week 5 | 内容创作提示词模板 | 创作API |
| Week 6 | 自动标签推荐 | 标签推荐API |
| Week 7 | 关联笔记推荐 | 推荐API |
| Week 8 | 流式对话支持 | SSE/WebSocket |

### Phase 3: 高级功能（4周）

| 周次 | 任务 | 交付物 |
|-----|------|-------|
| Week 9 | 向量嵌入存储 | embeddings表 |
| Week 10 | 语义搜索（向量检索） | /api/ai/semantic-search |
| Week 11 | 多轮对话上下文 | 对话管理 |
| Week 12 | 提示词模板管理 | 模板系统 |

---

## 六、依赖项

### 6.1 C++依赖（新增）

| 库 | 用途 | 获取方式 |
|---|------|---------|
| libcurl | HTTP客户端（调用Ollama） | vcpkg / 系统自带 |
| openssl | HTTPS支持 | vcpkg / 系统自带 |

### 6.2 前端依赖（新增）

| 包 | 用途 | 版本 |
|---|------|------|
| (无新增) | - | - |

### 6.3 外部服务

| 服务 | 用途 | 必需 |
|-----|------|------|
| Ollama | 本地LLM推理 | 可选 |
| OpenAI API | 在线LLM | 可选 |

---

*文档版本: v2.0*
*创建日期: 2026-04-29*
*更新日期: 2026-05-04*
*技术栈变更: Tauri/TypeScript → C++后端(Crow + SQLite)*
*状态: 设计文档，待实现*
