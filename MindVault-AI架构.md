# MindVault AI集成架构文档

## 一、AI功能架构

### 1.1 AI能力分层

```
┌─────────────────────────────────────────────────────────┐
│                  用户交互层                                 │
│  对话面板 | 内容创作 | 知识梳理 | 辅助工具                │
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

### 1.2 本地LLM集成

#### Ollama集成方案

```typescript
// src/main/services/ai/ollama.service.ts
export class OllamaService {
    private baseUrl: string = 'http://localhost:11434';

    // 检查Ollama是否可用
    async isAvailable(): Promise<boolean> {
        try {
            const res = await fetch(`${this.baseUrl}/api/tags`);
            return res.ok;
        } catch {
            return false;
        }
    }

    // 获取可用模型列表
    async getModels(): Promise<string[]> {
        const res = await fetch(`${this.baseUrl}/api/tags`);
        const data = await res.json();
        return data.models?.map((m: any) => m.name) || [];
    }

    // 流式生成（用于对话）
    async *generateStream(
        model: string,
        prompt: string,
        context?: number[]
    ): AsyncGenerator<string> {
        const res = await fetch(`${this.baseUrl}/api/generate`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                model,
                prompt,
                stream: true,
                context
            })
        });

        const reader = res.body?.getReader();
        if (!reader) return;

        const decoder = new TextDecoder();
        while (true) {
            const { done, value } = await reader.read();
            if (done) break;
            const chunk = decoder.decode(value);
            const lines = chunk.split('\n').filter(l => l.trim());
            for (const line of lines) {
                const data = JSON.parse(line);
                if (data.response) yield data.response;
            }
        }
    }

    // 嵌入向量（用于RAG）
    async embed(text: string): Promise<number[]> {
        const res = await fetch(`${this.baseUrl}/api/embeddings`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                model: 'nomic-embed-text', // 专用嵌入模型
                prompt: text
            })
        });
        const data = await res.json();
        return data.embedding;
    }
}
```

### 1.3 RAG（检索增强生成）架构

```typescript
// src/main/services/ai/rag.service.ts
export class RAGService {
    constructor(
        private dbService: DBService,
        private embeddingService: EmbeddingService,
        private llmService: LLMService
    ) {}

    // 核心RAG问答
    async query(
        question: string,
        vaultId: string,
        options?: { maxResults?: number; model?: string }
    ): Promise<RAGResponse> {
        // 1. 向量检索相关知识
        const relevantDocs = await this.retrieveRelevantDocs(
            question, vaultId, options?.maxResults || 5
        );

        // 2. 构建增强提示词
        const augmentedPrompt = this.buildRAGPrompt(
            question, relevantDocs
        );

        // 3. 调用LLM生成答案
        const answer = await this.llmService.generate(
            augmentedPrompt, options?.model
        );

        // 4. 返回答案+引用来源
        return {
            answer,
            sources: relevantDocs.map(doc => ({
                noteId: doc.noteId,
                title: doc.title,
                snippet: doc.snippet,
                relevance: doc.score
            }))
        };
    }

    // 检索相关知识片段
    private async retrieveRelevantDocs(
        query: string,
        vaultId: string,
        maxResults: number
    ) {
        // 方案A: 向量检索（高级）
        // const queryEmbedding = await this.embeddingService.embed(query);
        // return await this.vectorSearch(queryEmbedding, vaultId, maxResults);

        // 方案B: FTS5关键词检索（MVP，简单有效）
        const ftsResults = await this.dbService.searchFTS(query, vaultId, maxResults);

        return ftsResults.map(r => ({
            noteId: r.id,
            title: r.title,
            snippet: this.extractSnippet(r.content, query),
            score: r.rank || 0
        }));
    }

    // 构建RAG提示词
    private buildRAGPrompt(question: string, docs: any[]): string {
        const context = docs.map((doc, i) =>
            `[来源${i+1}: ${doc.title}]\n${doc.snippet}`
        ).join('\n\n');

        return `你是一个知识库助手。请基于以下参考内容回答问题。
如果参考资料无法回答，请明确说明，不要编造信息。

## 参考资料：
${context}

## 问题：
${question}

## 回答：`
    }

    // 提取相关片段
    private extractSnippet(content: string, query: string, contextLen = 100): string {
        const lowerContent = content.toLowerCase();
        const lowerQuery = query.toLowerCase();
        const idx = lowerContent.indexOf(lowerQuery);
        if (idx === -1) return content.substring(0, 200) + '...';

        const start = Math.max(0, idx - contextLen);
        const end = Math.min(content.length, idx + query.length + contextLen);
        return (start > 0 ? '...' : '') +
               content.substring(start, end) +
               (end < content.length ? '...' : '');
    }
}
```

### 1.4 AI功能模块详细设计

#### 功能模块1: 内容创作

```typescript
// 提示词模板
const CONTENT_TEMPLATES = {
    outline: (topic: string) => `请为主题"${topic}"生成一个详细的文章大纲，包含：
1. 引言
2. 主体部分（至少3个主要章节）
3. 结论
要求：结构清晰，逻辑严密。`,

    expand: (text: string) => `请将以下内容扩写，增加细节和例子：
"${text}"
要求：保持原意，语言流畅，内容充实。`,

    summarize: (text: string) => `请将以下内容总结为3-5个要点的摘要：
"${text}"`,

    polish: (text: string, style: string) => `请将以下文字润色为${style}风格：
"${text}"`,

    translate: (text: string, targetLang: string) => `请将以下文字翻译为${targetLang}：
"${text}"`
};
```

#### 功能模块2: 知识梳理

```typescript
// 自动标签推荐
async recommendTags(noteContent: string, existingTags: string[]): Promise<string[]> {
    const prompt = `分析以下笔记内容，从已有标签中选择最相关的3-5个标签。
如果都不合适，可以推荐新标签（以"新:"开头）。

已有标签：${existingTags.join(', ')}

笔记内容：
${noteContent.substring(0, 1000)}...

请返回JSON数组格式，例如：["标签1", "标签2", "新:新标签3"]`;

    const response = await this.llmService.generate(prompt);
    try {
        return JSON.parse(response);
    } catch {
        return [];
    }
}

// 关联笔记推荐
async recommendRelatedNotes(
    currentNote: Note,
    allNotes: Note[],
    limit = 5
): Promise<NoteRecommendation[]> {
    // 基于内容相似度推荐
    const prompt = `分析当前笔记，从以下笔记列表中推荐最相关的${limit}篇：

当前笔记标题：${currentNote.title}
当前笔记内容：${currentNote.content.substring(0, 500)}...

候选笔记列表：
${allNotes.map((n, i) => `${i+1}. ${n.title}`).join('\n')}

请以JSON格式返回推荐结果：
[{"noteId": "id", "reason": "推荐理由", "relevance": 0.95}]`;

    const response = await this.llmService.generate(prompt);
    // 解析并返回
    return JSON.parse(response);
}
```

#### 功能模块3: 侧边栏AI对话

```typescript
// src/renderer/components/ai/AIChatPanel.vue
interface ChatMessage {
    id: string;
    role: 'user' | 'assistant';
    content: string;
    sources?: SourceReference[];  // RAG引用来源
    timestamp: number;
}

// 对话逻辑
const chatStore = useAIChatStore();

async function sendMessage(content: string) {
    // 添加用户消息
    chatStore.addMessage({ role: 'user', content });

    // 构建上下文（当前笔记 + 历史对话）
    const context = buildContext();

    // 调用RAG查询
    const response = await window.api.ai.query(content, currentVaultId, {
        context,
        noteId: currentNote.value?.id
    });

    // 添加AI回复
    chatStore.addMessage({
        role: 'assistant',
        content: response.answer,
        sources: response.sources
    });
}

function buildContext() {
    return {
        currentNote: currentNote.value ? {
            id: currentNote.value.id,
            title: currentNote.value.title,
            content: editorContent.value?.substring(0, 2000)
        } : undefined,
        history: chatStore.messages.slice(-6)  // 最近3轮对话
    };
}
```

---

## 二、AI模型兼容列表

### 2.1 本地模型（Ollama）

| 模型 | 大小 | 用途 | 推荐场景 |
|-----|------|------|---------|
| **Qwen2.5:7b** | 4.7GB | 通用对话 | 首选，中文友好 |
| **Llama 3.2:3b** | 2.0GB | 轻量对话 | 低配置机器 |
| **Phi-3:mini** | 2.3GB | 快速推理 | 简单任务 |
| **nomic-embed-text** | 274MB | 向量嵌入 | RAG必备 |

### 2.2 在线API

| 服务商 | 模型 | 特点 | 适用场景 |
|-------|-----|------|---------|
| OpenAI | gpt-4o | 能力强 | 复杂推理、创作 |
| OpenAI | gpt-4o-mini | 快+便宜 | 日常对话 |
| 通义千问 | qwen-max | 中文强 | 中文内容处理 |
| 通义千问 | qwen-plus | 性价比 | 常规任务 |
| 豆包 | doubao-pro | 中文优 | 中文润色 |

---

## 三、AI配置管理

```typescript
// AI配置结构
interface AISettings {
    // 模式选择
    mode: 'local' | 'api' | 'hybrid';

    // 本地LLM设置
    local: {
        enabled: boolean;
        baseUrl: string;  // Ollama地址
        model: string;     // 默认模型
        embeddingModel: string;
    };

    // API设置
    api: {
        provider: 'openai' | 'tongyi' | 'doubao';
        apiKey: string;
        baseUrl?: string;
        model: string;
    };

    // 提示词模板
    templates: {
        [key: string]: {
            name: string;
            prompt: string;
            variables: string[];  // 如 {{content}}, {{title}}
        };
    };
}
```

---

*文档版本: v1.0*
*创建日期: 2026-04-29*
