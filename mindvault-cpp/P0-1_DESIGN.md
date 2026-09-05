# P0-1: AI 问答多轮会话记忆 - 设计文档

## 1. 背景与目标

**现状**：`/api/ai/chat` 无状态，每次请求独立，无上下文。用户问"那第二个方案呢"这类指代性问题会断链。

**目标**：引入会话管理，前端支持新建/切换/删除会话，后端在会话内自动拼接最近 N 轮历史，使模型能理解指代。

**约束**：
1. 会话按用户隔离（`user_id`），不能串库
2. 历史消息存储、拼接轮数、Token 截断策略按本文档方案
3. `/api/ai/chat` 现有单轮调用保持兼容，不改坏
4. 前端 `AIChatPanel.vue` 改造支持会话列表，UI 风格保持一致

---

## 2. 数据库设计

### 2.1 新增两张表

```sql
-- 会话表
CREATE TABLE IF NOT EXISTS ai_conversations (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id       INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    title         TEXT NOT NULL DEFAULT '新对话',
    created_at    TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    updated_at    TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);

CREATE INDEX IF NOT EXISTS idx_ai_conversations_user ON ai_conversations(user_id);
CREATE INDEX IF NOT EXISTS idx_ai_conversations_updated ON ai_conversations(updated_at DESC);

-- 消息表
CREATE TABLE IF NOT EXISTS ai_messages (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    conversation_id INTEGER NOT NULL REFERENCES ai_conversations(id) ON DELETE CASCADE,
    role            TEXT NOT NULL CHECK (role IN ('user','assistant','system')),
    content         TEXT NOT NULL,
    tokens          INTEGER DEFAULT 0,           -- 该条消息估算 token 数（可选，用于截断决策）
    created_at      TEXT NOT NULL DEFAULT (datetime('now','localtime'))
);

CREATE INDEX IF NOT EXISTS idx_ai_messages_conv ON ai_messages(conversation_id, created_at);
```

**设计说明**：
- `user_id` 外键关联 `users` 表，级联删除保证用户删除时会话一并清理
- `title` 默认"新对话"，首轮用户提问后可异步生成摘要标题（后续迭代）
- `tokens` 字段预留，便于后期精确截断；MVP 可先不填，按字符数估算
- 索引覆盖：按用户查会话列表、按会话查消息、按更新时间排序

### 2.2 database.cpp 修改点

在 `Database::Init()` 中追加上述两个 `CREATE TABLE` 与索引。参考现有 `notes` 表迁移逻辑，新表无需迁移（全新建表）。

---

## 3. 后端 API 设计

### 3.1 新增会话管理 API（`/api/ai/conversations`）

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/ai/conversations` | 列表：当前用户的会话，按 `updated_at DESC`，含最近一条预览 |
| POST | `/api/ai/conversations` | 新建：`{title?}` → 返回 `{id, title, created_at, updated_at}` |
| GET | `/api/ai/conversations/:id` | 详情：含完整消息历史（分页） |
| PUT | `/api/ai/conversations/:id` | 重命名：`{title}` |
| DELETE | `/api/ai/conversations/:id` | 删除：级联删消息 |
| POST | `/api/ai/conversations/:id/messages` | 追加消息（内部用，前端不直接调） |

### 3.2 修改现有 `/api/ai/chat`（保持兼容）

**请求体新增可选字段**：
```json
{
  "question": "那第二个方案呢",
  "provider": "tongyi",
  "model": "qwen-plus",
  "api_key": "...",
  "api_url": "",
  "conversation_id": 123   // 新增：可选，传则进入会话模式；不传则单轮兼容模式
}
```

**行为差异**：
| 场景 | conversation_id | 行为 |
|------|----------------|------|
| 单轮兼容 | 无/空 | 现有逻辑：仅 RAG + 当前 question 发模型，不存历史 |
| 会话模式 | 有效 ID | 1. 校验会话归属 user_id<br>2. 读取该会话最近 N 轮历史<br>3. 拼接：system + history + RAG context + 当前 question<br>4. 调用模型<br>5. 写入 user/assistant 两条消息<br>6. 更新会话 `updated_at` |

**响应体新增字段**（兼容旧字段）：
```json
{
  "answer": "...",
  "sources": [...],
  "provider": "tongyi",
  "model": "qwen-plus",
  "conversation_id": 123,      // 新增：会话模式时返回
  "message_id": 456            // 新增：assistant 消息 ID
}
```

### 3.3 请求/响应结构定义

**会话列表项**：
```typescript
interface ConversationListItem {
  id: number;
  title: string;
  updated_at: string;
  last_message_preview: string;  // 最近一条消息前 50 字
  message_count: number;
}
```

**会话详情**：
```typescript
interface ConversationDetail {
  id: number;
  title: string;
  created_at: string;
  updated_at: string;
  messages: ChatMessage[];  // 见下
}
```

**消息**：
```typescript
interface ChatMessage {
  id: number;
  role: 'user' | 'assistant' | 'system';
  content: string;
  created_at: string;
}
```

### 3.4 历史拼接与 Token 截断策略

**参数（写死在代码常量，不配置化）**：
- `MAX_HISTORY_TURNS = 10`：最多取最近 10 轮（user+assistant 各 10 条，共 20 条）
- `MAX_CONTEXT_TOKENS = 12000`：留给历史+RAG+question 的总 token 预算（模型上下文窗口减去 system+output 预留）

**截断算法**：
1. 从最新往旧取消息，直到达到 `MAX_HISTORY_TURNS` 或累计估算 token 超过 `MAX_CONTEXT_TOKENS`
2. 估算公式：`tokens ≈ chars / 2.5`（中英混合经验值），`tokens` 字段若为 0 则用此估算
3. 截断时**保留 system prompt + RAG context + 当前 question**，只砍历史
4. 被截断的早期历史不删除，仅不发给模型

**拼接顺序**：
```
[system prompt]
[history user/assistant ...]  // 已截断
[RAG context]               // 固定在最后一轮 user 前
[user: current question]
```

---

## 4. 前端改造

### 4.1 文件变更清单

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `web/src/api.ts` | 新增 | `conversationsApi` 对象：list/create/get/rename/delete |
| `web/src/components/AIChatPanel.vue` | 重构 | 核心改造：左侧会话列表 + 右侧消息区，状态提升到面板级 |
| `web/src/App.vue` | 微调 | 传递 `userId` 给 AIChatPanel（从 auth 状态取） |

### 4.2 AIChatPanel.vue 重构要点

**状态结构变更**：
```typescript
// 旧：messages: ChatMessage[]
// 新：
conversations: ConversationListItem[]
activeConversationId: number | null
messages: ChatMessage[]          // 当前会话的消息
loading: boolean
```

**UI 布局**：
```
┌─────────────────────────────────────┐
│ AI 助手  [provider] [model] ⚙️ 🗑️ ×  │  ← 头部保持
├──────────┬──────────────────────────┤
│ 会话列表  │  消息区                   │
│          │  - 空状态/消息列表         │
│ + 新建    │  - 加载中                 │
│ 💬 新对话  │                           │
│ 📝 对话1  │  输入区                   │
│ 📝 对话2  │  [textarea] [发送]        │
│ ...      │                           │
└──────────┴──────────────────────────┘
```

**关键交互**：
- 点击会话项 → 加载该会话消息（调用 `/conversations/:id`）
- "新建对话" → POST `/conversations` → 切换到新会话
- 发送消息：
  - 若 `activeConversationId` 存在 → 带 `conversation_id` 调用 `/api/ai/chat`
  - 若无 → 先创建会话，再发送（或直接单轮兼容模式）
- 删除会话 → 确认后 DELETE → 若删的是当前会话，清空消息区

**消息渲染复用**：现有 `ai-message` 样式复用，只加会话列表侧边栏样式。

### 4.3 api.ts 新增接口

```typescript
export interface ConversationListItem {
  id: number
  title: string
  updated_at: string
  last_message_preview: string
  message_count: number
}

export interface ConversationDetail {
  id: number
  title: string
  created_at: string
  updated_at: string
  messages: ChatMessage[]
}

export interface ChatMessage {
  id: number
  role: 'user' | 'assistant' | 'system'
  content: string
  created_at: string
}

export const conversationsApi = {
  list: () => request<ConversationListItem[]>('/ai/conversations'),
  create: (title?: string) => request<ConversationListItem>('/ai/conversations', { method: 'POST', body: JSON.stringify({ title }) }),
  get: (id: number) => request<ConversationDetail>(`/ai/conversations/${id}`),
  rename: (id: number, title: string) => request<void>(`/ai/conversations/${id}`, { method: 'PUT', body: JSON.stringify({ title }) }),
  delete: (id: number) => request<void>(`/ai/conversations/${id}`, { method: 'DELETE' }),
}
```

---

## 5. 实现步骤建议

### Phase 1：数据库 + 后端骨架
1. `database.cpp` 加两表建表 SQL
2. `ai_routes.h` 新增 `RegisterAIConversationRoutes`，实现 5 个会话管理端点
3. 修改 `/api/ai/chat` 读取 `conversation_id`，实现历史拼接与写入
4. 单元测试：会话 CRUD、历史拼接截断、归属校验

### Phase 2：前端 API + 面板重构
1. `api.ts` 增 `conversationsApi`
2. `AIChatPanel.vue` 拆分：`ConversationSidebar.vue`（新组件）+ 消息区
3. 状态提升、交互联调

### Phase 3：兼容性验证
- 无 `conversation_id` 请求仍按单轮走通
- 旧前端（若有）不传新字段不报错
- 多用户隔离：user A 看不到 user B 会话

---

## 6. 风险点与未考虑点 ⚠️

| 编号 | 风险/盲区 | 缓解建议 |
|------|-----------|----------|
| R1 | **Token 估算不准**：中文按 1.5-2 字/token，英文 4 字/token，定死 `chars/2.5` 可能超限或浪费 | MVP 先用估算，日志记录实际 token（若模型返回 usage），后期引入 `tiktoken` 或模型侧 `usage` 回传校准 |
| R2 | **System prompt 硬编码在 C++** | 现状已在 `ai_routes.h:108` 硬编码。建议抽到配置表或常量，便于后续按会话自定义 system prompt |
| R3 | **RAG context 重复注入**：每轮都把同一份检索结果塞进上下文，token 浪费 | 可选优化：仅首轮注入 RAG，后续轮次仅靠历史隐式传递；或在 prompt 里标注"参考资料同上" |
| R4 | **并发安全**：同一会话并发两个请求，历史读取/写入竞争 | SQLite WAL + 事务包裹 `读历史→拼装→调模型→写两条消息→更新会话时间`，或用会话级 mutex（`std::mutex` map keyed by conv_id） |
| R5 | **会话标题自动生成**：目前固定"新对话"，体验差 | 后续迭代：首轮结束后异步请求小模型生成 10 字标题，`PUT /conversations/:id` 更新 |
| R6 | **前端会话列表分页/虚拟化**：用户会话数可能达百 | 先不分页（SQLite 查 100 行极快），超 200 再加 `limit/offset` 或虚拟滚动 |
| R7 | **迁移现有用户数据**：现有用户无会话记录，首次打开面板列表为空 | 无需迁移，自然为空即可 |
| R8 | **删除会话时级联删消息**：SQLite 外键 `ON DELETE CASCADE` 需 `PRAGMA foreign_keys=ON`（已在 Database 构造函数开启） | 验证一下 `sqlite3_open16` 路径下 foreign_keys 依然生效 |
| R9 | **流式输出**：现有 `/api/ai/chat` 非流式，若后续加 SSE/流式，会话写入时机需调整 | 先不做流式，文档标记为后续扩展点 |
| R10 | **API Key 存储**：前端存在 localStorage，后端不存。会话模式下每次请求仍需前端传 key | 可选：后端加密存 key，会话关联 provider 配置，减少前端传递。MVP 维持现状 |

---

## 7. 验收标准

1. **单轮兼容**：不传 `conversation_id`，原有单轮问答完全不受影响
2. **会话隔离**：用户 A 创建会话，用户 B 列表不可见、无法访问
3. **历史生效**：同一会话连续问"第一个方案是什么"→"那第二个呢"，第二轮能正确指代
4. **截断生效**：超 10 轮或超 token 预算时，早期历史不再发给模型（日志可观测）
5. **前端交互**：新建/切换/删除/重命名会话流程走通，UI 无回归
6. **性能**：会话列表 < 50ms，消息加载 < 100ms（本地 SQLite）

---

## 8. 里程碑

| 里程碑 | 产出 |
|--------|------|
| M1 | 数据库建表 + 后端会话 CRUD + `/api/ai/chat` 会话模式可跑通 |
| M2 | 前端会话列表 + 切换 + 发送联调通过 |
| M3 | 兼容性测试通过、文档更新、可发布 |

---

*文档版本：v1.0*
*作者：AI Assistant*
*日期：2026-09-05*