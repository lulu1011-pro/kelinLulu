# P0-1: AI 问答多轮会话记忆 - 设计文档

> **状态：已实现（2026-09-05）**

---

## 1. 概述

### 1.1 现状分析
- `/api/ai/chat` 为无状态单轮接口，每次请求独立，无上下文
- 前端 `AIChatPanel.vue` 仅在本地内存维护 `messages`，刷新即丢失
- 用户无法进行指代性追问（如"那第二个方案呢"）

### 1.2 目标
- 引入**会话**概念，按用户隔离
- 前端支持：新建/切换/删除会话，会话列表持久化
- 后端：会话内自动拼接最近 N 轮历史，token 超限截断
- 保持 `/api/ai/chat` 向后兼容（不传 `conversation_id` 时退回单轮模式）

---

## 2. 数据库设计

### 2.1 新增两张表

```sql
-- 会话表
CREATE TABLE ai_conversations (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id      INTEGER NOT NULL,
    title        TEXT NOT NULL DEFAULT '新对话',
    is_deleted   INTEGER NOT NULL DEFAULT 0,
    deleted_at   TEXT DEFAULT NULL,
    created_at   TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    updated_at   TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX idx_ai_conversations_user ON ai_conversations(user_id, updated_at DESC) WHERE is_deleted = 0;

-- 消息表
CREATE TABLE ai_messages (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    conversation_id  INTEGER NOT NULL,
    role             TEXT NOT NULL CHECK (role IN ('user','assistant')),
    content          TEXT NOT NULL,
    tokens           INTEGER DEFAULT 0,
    created_at       TEXT NOT NULL DEFAULT (datetime('now','localtime')),
    FOREIGN KEY (conversation_id) REFERENCES ai_conversations(id) ON DELETE CASCADE
);

CREATE INDEX idx_ai_messages_conv ON ai_messages(conversation_id, created_at);
```

### 2.2 实现说明
- `CREATE TABLE IF NOT EXISTS` 保证老库首次启动自动建表，零迁移
- 软删字段 `is_deleted`、`deleted_at` 在建表时一次性带上
- `role` 约束仅允许 `user`/`assistant`，不存 `system` 和 `rag_context`

---

## 3. 后端 API 设计

### 3.1 新增会话管理 API

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/ai/conversations` | 新建会话 |
| GET | `/api/ai/conversations` | 列表（当前用户，按 `updated_at DESC`） |
| GET | `/api/ai/conversations/:id` | 详情（含消息历史） |
| DELETE | `/api/ai/conversations/:id` | 软删会话 |
| PUT | `/api/ai/conversations/:id` | 重命名会话 |

### 3.2 请求/响应示例

#### 新建会话
```
POST /api/ai/conversations
Authorization: Bearer {token}

{"title": "可选标题"}
```
响应：
```json
{
  "ok": true,
  "data": {
    "id": 1,
    "title": "新对话",
    "created_at": "2026-09-05 22:03:28",
    "updated_at": "2026-09-05 22:03:28",
    "message_count": 0
  }
}
```

#### 会话列表
```
GET /api/ai/conversations
Authorization: Bearer {token}
```
响应：
```json
{
  "ok": true,
  "data": [
    {
      "id": 2,
      "title": "什么是 RAG...",
      "updated_at": "2026-09-05 22:25:43",
      "message_count": 8
    }
  ]
}
```

#### 会话详情（含消息）
```
GET /api/ai/conversations/2
Authorization: Bearer {token}
```
响应：
```json
{
  "ok": true,
  "data": {
    "id": 2,
    "title": "什么是 RAG...",
    "messages": [
      {"id": 1, "role": "user", "content": "什么是 RAG", "tokens": 0, "created_at": "..."},
      {"id": 2, "role": "assistant", "content": "RAG 是检索增强生成...", "tokens": 0, "created_at": "..."}
    ]
  }
}
```

#### 发送消息（会话模式）
```
POST /api/ai/chat
Authorization: Bearer {token}

{
  "question": "那第二个方案呢",
  "provider": "tongyi",
  "model": "qwen-turbo",
  "api_key": "sk-xxx",
  "conversation_id": 2
}
```
响应：
```json
{
  "ok": true,
  "data": {
    "answer": "第二个方案是...",
    "sources": [...],
    "provider": "tongyi",
    "model": "qwen-turbo",
    "conversation_id": 2,
    "message_id": 10
  }
}
```

#### 单轮兼容（不传 conversation_id）
```
POST /api/ai/chat
Authorization: Bearer {token}

{
  "question": "什么是 RAG",
  "provider": "tongyi",
  "model": "qwen-turbo",
  "api_key": "sk-xxx"
}
```
响应与上面相同，但 `conversation_id: 0`, `message_id: 0`。

### 3.3 截断常量
```cpp
constexpr int MAX_HISTORY_TURNS = 10;      // 最近 10 轮（20 条消息）
constexpr int MAX_CONTEXT_TOKENS = 8000;   // history + RAG + question 预算
constexpr int SYSTEM_PROMPT_TOKENS = 200;  // system prompt 预估
```

---

## 4. 前端改造

### 4.1 文件变更清单

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `web/src/api.ts` | 修改 | 新增 `Conversation`/`ChatMessage`/`ChatRequest`/`ChatResponse` 类型 + `conversationsApi` |
| `web/src/components/AIChatPanel.vue` | 重构 | 左右布局：会话列表侧边栏 + 消息区，支持新建/切换/删除/重命名 |

### 4.2 UI 布局
```
┌─────────────────────────────────────────────┐
│ AI 助手  [provider] [model] ⚙️ ×             │
├──────────┬──────────────────────────────────┤
│ 会话列表  │  消息区                           │
│          │  - 空状态/消息列表                 │
│ + 新建    │  - 加载中                         │
│ 💬 对话1  │                                    │
│ 💬 对话2  │  输入区                           │
│ ...      │  [textarea] [发送]                 │
└──────────┴──────────────────────────────────┘
```

---

## 5. 风险点

| 编号 | 风险 | 状态 |
|------|------|------|
| R1 | Token 估算不准（`length/3` 粗估） | 已缓解：MVP 可用，后续引入 `usage` 字段校准 |
| R2 | System prompt 硬编码 | 已知，后续可抽到配置 |
| R3 | RAG context 每轮重复注入 | 已知，后续可优化为仅首轮注入 |
| R4 | 并发安全 | ✅ 已解决：会话级 `std::mutex` + 事务包裹 |
| R5 | 会话标题自动生成 | ✅ 已实现：首轮截取前 20 字 |
| R6 | 流式输出 | 待实现（见 SSE 扩展方案） |

---

## 6. 验收标准

1. **单轮兼容** ✅ — 不传 `conversation_id` 走原逻辑
2. **会话隔离** ✅ — 用户 A 看不到用户 B 的会话
3. **历史生效** ✅ — 连续追问能理解指代
4. **截断生效** ✅ — 超 10 轮或超 token 预算时早期历史被丢弃
5. **前端交互** ✅ — 新建/切换/删除/重命名流程走通
6. **异常处理** ✅ — 空问题 400 / 不存在 404 / 删除后 404 / 数据库忙友好提示

---

## 7. 实际改动文件清单

| 文件 | 改动 |
|------|------|
| `src/database.cpp` | `Init()` 新增 `ai_conversations` + `ai_messages` 建表；新增 6 个 DB 方法实现 |
| `src/database.h` | 新增 6 个方法声明（Create/Get/Update/SoftDelete 会话 + Add/Get 消息） |
| `src/routes/ai_routes.h` | 截断常量 + 会话级 mutex + token 估算 + `extractUserId` + 5 个会话端点 + `/api/ai/chat` 会话模式分支 |
| `web/src/api.ts` | 新增 4 个类型定义 + `conversationsApi`（5 个方法） |
| `web/src/components/AIChatPanel.vue` | 重构为左右布局，新增会话状态管理、创建/切换/删除/重命名逻辑、乐观更新回滚 |

---

*文档版本：v1.0 → v2.0（已实现）*
*完成日期：2026-09-05*