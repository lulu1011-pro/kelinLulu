# MindVault AI 功能增强设计（面试价值导向 · 纯 API 版）

> 文档版本：v1.4 ｜ 创建日期：2026-09-05 ｜ 状态：已实现（P0-1 多轮会话 / P0-2 SSE 流式 / P0-3.1 Query 改写 / P0-4 Token 预算 / P1-5 混合检索 / P1-6 内容创作 / P1-7 自动标签+关联推荐 / P2 图谱问答+function calling，实现日期 2026-09-07）
> 前提约束：不使用本地模型（Ollama 不装），全部走 OpenAI 兼容在线 API（通义 / 豆包 / OpenAI / 自定义已支持）。
> 设计目标：每个功能都要能扛住面试官追问到第 3 层以上，而不是堆砌"我有这个功能"。

---

## 〇、现状盘点（代码核实，2026-09-05）

已实现的 AI 能力（在 `src/routes/ai_routes.h`）：

| 能力 | 状态 | 说明 |
|------|------|------|
| POST /api/ai/chat | ✅ 已实现 | 非流式。FTS5 Search(5) → 拼"参考资料+问题" → 调在线 API → 返回 answer + sources |
| GET /api/ai/status | ✅ 已实现 | 探测 ollama / 列出 tongyi、doubao、openai、custom 四种 provider |
| 拒答提示 | ✅ 已实现 | prompt 里写"无法回答请明确说明，不要编造" |
| 前端 AIChatPanel | ✅ 已实现 | 输入 → 非流式展示 answer + sources |

数据库现状：`users / notes / tags / note_tags / link_edges / versions / flashcards / permissions` —— **无对话表、无向量表、无 embedding 存储**。

结论：当前 AI = "单轮 FTS5-RAG 问答"。面经高频的上下文管理、多轮、流式、检索质量、知识梳理，全部空白。

---

## 一、功能取舍原则（先讲为什么）

面试官对 AI 项目的追问，集中在**检索质量、上下文处理、工程化**这三类，而不是"你接了几个模型"。所以：

1. **加能展示"深度"的，不加能展示"数量"的**：多接一个 provider 不算亮点，把一条检索链做到位才算。
2. **每个功能必须能在面经里被问**（下方每项都标了对应面经题）。
3. **复用已有资产**：你有 FTS5、link_edges 双向链接图、tags、versions——AI 功能要和这些真实结构结合，面试时才能说"这是我系统里真实的数据，不是 demo"。
4. **不做清单**：不硬塞 Agent/多 Agent 编排（和 RAG 知识库产品定位不符，硬凑会被问穿）；不做本地微调；不做自训 embedding 模型。

优先级：
- **P0（必做，2 周）**：多轮对话+会话管理、流式输出 SSE、RAG 质量三件套（query 改写、引用溯源、拒答升级）
- **P1（强烈建议，1-2 周）**：混合检索（FTS5+API 向量）+ 重排、内容创作套件、自动标签+关联笔记推荐
- **P2（可选加分）**：知识图谱问答、function calling 轻量用法、长文异步摘要

---

## 二、P0-1：多轮对话 + 会话管理

### 功能描述
聊天从"单问单答"升级为"会话制"：能开多个会话、每轮上下文连贯、刷新不丢历史、能引用上一轮提到的笔记。现在 `/api/ai/chat` 每次是无状态单轮，问"第二点是什么"模型根本不知道第一点。

### 表设计（新增两张表）

```
chat_sessions(
    id INTEGER PK,
    user_id INTEGER,            -- 每用户独立库，天然隔离
    title TEXT,                 -- 自动用首问生成
    provider TEXT, model TEXT,  -- 记住这次会话用的模型
    created_at TEXT, updated_at TEXT)

chat_messages(
    id INTEGER PK,
    session_id INTEGER,         -- 属于哪个会话
    role TEXT,                  -- 'user' | 'assistant'
    content TEXT,
    sources_json TEXT,          -- 这轮答案引用了哪些笔记（存结构化）
    token_count INTEGER,        -- 这轮消耗的 token（做预算用，见 P0-4）
    created_at TEXT)
```

### 请求/响应扩展
- `POST /api/ai/chat`：body 增加 `session_id`；没有则后端新建会话并返回 id。
- `GET /api/ai/sessions`、`GET /api/ai/sessions/{id}/messages`、`DELETE /api/ai/sessions/{id}`：会话列表与历史。

### 多轮上下文怎么拼（核心技术点）
请求上游前，取出该会话最近消息，按"系统提示词 + 最近 N 轮 + 本轮检索的参考资料"顺序拼接。N 由 token 预算决定（见 P0-4）。**要能讲清：不是把全部历史无脑塞进去，而是滚窗 + 超预算截断。**

### 面试价值 / 追问链
- 对应面经题（真实出现）："多轮对话（多轮查询）有研究/看过吗？你的项目用到这块了吗？""上下文管理具体怎么做、用了什么工具？"
- 第 1 层：多轮就是把历史消息一起发给模型
- 第 2 层：历史不能无限长，受 token 限制 → 需要管理策略
- 第 3 层：为什么用"截断最旧"而不是"随机丢"？最近上下文对当前回答影响最大
- 第 4 层：更早的、被截断的内容怎么补救？→ 摘要压缩（P2）或让它回到检索里

---

## 三、P0-2：流式输出（SSE）

### 功能描述
回答一个字一个字蹦出来，而不是等 5 秒整段出现。面经里明确把"流式输出（SSE）、对话消息列表、加载态、错误处理"绑在一起当考察点。

### 技术方案
- 前端 `fetch` 带上 `stream: true` 语义的请求，后端返回 `Content-Type: text/event-stream`，数据按 SSE 分块推给前端（每块 `data: {...}\n\n`），前端用 ReadableStream 边收边渲染。
- 上游（通义/OpenAI/豆包）本身就支持 `stream: true` 流式返回——后端做的是**透传转发**：收到上游一个 chunk 就立刻推给浏览器，不要攒满再发。
- 需要处理：连接中断、上游报错（错误也要以 SSE 事件推给前端）、结束标记 `[DONE]`。

### 面试价值 / 追问链
- 对应面经题："流式输出（SSE）、对话消息列表、输入交互、错误处理、加载态"（多份面经列为 AI 前端必考点）。
- 第 1 层：SSE 是服务端单向推送，基于 HTTP 长连接
- 第 2 层：SSE vs WebSocket 区别——为什么这里选 SSE？（单向够用、基于 HTTP 不用升级协议、断线自动重连是浏览器内置行为；WebSocket 是双向全双工，聊天要双向才用它）
- 第 3 层：你的后端怎么转发？上游一个 chunk 来就立刻推，不做缓冲——因为缓冲会吃掉流式最大的价值"首字延迟低"
- 第 4 层：首字延迟(TTFT)为什么是流式体验的关键指标

---

## 四、P0-3：RAG 质量三件套

这三个是"让 RAG 从能跑变成能打"的关键，也是面试官深挖检索质量时必问的。

### 4.1 query 改写（Query Rewrite）
用户问"那第二个方案呢？"——这句没有上下文根本搜不到东西。做法：把**最近对话历史 + 当前问题**一起发给模型，让它改写成一个**独立的、含完整语义的搜索 query**，再用改写后的 query 去 FTS5/向量检索。

- 对应面经题（真实出现）："如何润色用户的 Query（Query Rewrite）？目的是什么？"
- 能讲：改写解决"指代消解 + 省略补全"，让检索词从口语变关键词；用**便宜的模型/一次小调用**做改写，不用主模型。

### 4.2 引用溯源升级
现在已返回 sources，但只是"贴 5 条结果"。升级为：模型回答时按 [1][2] 标引用，sources 里给出每条引用命中的**笔记 id、标题、原文片段**，前端点击可跳转到对应笔记。

- 对应面经题：RAG 流程必讲"附上引用来源，就是溯源"；"检索到有害/过期文档怎么办"也是同一簇。
- 能讲：prompt 里要求"只依据 [来源N] 的内容回答，并在句末标注 [N]"；后端正则解析 [N] 与来源列表对齐；回答里没引用的来源不下发前端——**防的是模型编引用**。

### 4.3 拒答升级
现在只在 prompt 里写"答不出就说不知道"。升级成可解释的拒答：模型判断资料不足时，回答必须固定以"知识库中没有相关内容"开头，后端检测到该前缀就把回复标记为 `refused: true`，前端展示"没搜到，要不要换个问法/新建笔记"。

- 能讲：拒答阈值其实是产品决策——宁可说不知道，不编造（面经考点"AI 幻觉怎么治"的落地版）。

### 面试价值（三件套整体）
这条链正好覆盖面经里 RAG 深度全套：切块 → 检索 → 改写 → 重排 → 生成 → 溯源 → 拒答。每一项都能往下钻一层，是**你 AI 项目最值得炫耀的技术纵深**。

---

## 五、P0-4：上下文预算与超限处理（和 P0-1 配套）

### 功能描述
多轮对话做起来后，必须处理"拼的东西超过模型上下文窗口"。

### 技术方案（递进三层，面试按层答）
1. **token 估算**：每条消息存 token_count（可调上游接口拿 usage，或用字符数×系数粗估中文约 1 字≈0.6-0.7 token）。请求前累加，超预算就从最旧消息开始丢，保住系统提示词和本轮检索资料。
2. **优先级排序**：保留顺序是 系统提示词 > 本轮检索上下文 > 最近的对话 > 较早的对话。丢了较早对话后，把被丢内容的**一句话摘要**放在最前面兜底（见 3）。
3. **摘要压缩（P2 可先不做）**：每满 N 轮，调一次模型把之前对话压成摘要存进会话记录，长会话不失控。

### 面试价值
- 对应面经题："上下文窗口""token 限制""上下文超限怎么切分处理""本地存储累积上下文后有没有做精简/摘要处理"——**这是 2026 AI 项目面试出现频率最高的深挖点**。
- 你的服务端 C++ 里能做"预算计算 + 丢弃决策"本身就是工程能力展示：不是把问题甩给前端，而是后端定策略。

---

## 六、P1-5：混合检索（FTS5 + API 向量）与重排

### 功能描述
现在只有 FTS5 关键词检索：搜"进程和线程区别"这种含关键词的没问题，但"为什么我电脑越来越卡"这种语义问法就搜不到记过的东西。加一路**向量检索**：把笔记切片 → 调 embedding API 转成向量 → 存库里 → 用户提问也转向量 → 算余弦相似度召回。两路结果融合，再用重排模型（rerank，可选）精排。

### 关键设计决策（每一条都是面试弹药）
1. **切块策略**：按什么切？固定字符数（如 500-800 字）还是按 Markdown 标题切？——答：先按标题/段落切，超长段再按窗口切，重叠 50-100 字防切断语义。面经直接考"RAG 的切块规则"。
2. **embedding 走 API**：不装本地模型。可选阿里 dashscope text-embedding-v3（有免费额度、中文好）、OpenAI text-embedding-3-small。**费用低到可忽略**（百万 token 级别几块钱）。
3. **向量存哪**：没有外部向量库，就用 SQLite 存向量文本（`note_chunks` 表：note_id、chunk_text、embedding_json），召回时**内存计算余弦**（万级 chunk 完全扛得住）——这正好呼应你"轻量、免部署"的定位，面试能讲"为什么不用 ES/专门的向量库：单机万级数据量，内存暴力算比引外部依赖更简单可靠"。面经必考"为什么用 SQLite/不用 ES"。
   - **量级演进路径（防追问"数据多了咋整"）**：万级 chunk → SQLite 存文本+内存暴力算余弦，毫秒级够用；十万级 → 上 SQLite 的 `sqlite-vec` 扩展做近似检索，仍免部署；百万级/需要高并发 QPS → 才考虑独立向量库（Milvus/Qdrant/Chroma）或 pgvector。**换库的决策依据是数据量×QPS 过了阈值，不是"别人说向量库好"**——这个演进思路本身也是面试加分回答。
4. **两路怎么融合**：关键词路和向量路各自取 TopN，用**分数归一化后加权求和**（如 RRF：`Σ 1/(k+rank)`），而不是简单拼接——能讲出 RRF 为什么鲁棒（不依赖两路分数尺度一致）。
5. **重排（可选）**：两路融合后 TopN 再送 rerank 模型精排。面经考"为什么要重排序(ReRank)？"

### 新表
```
note_chunks(
    id INTEGER PK,
    note_id INTEGER,        -- 属于哪篇笔记
    chunk_text TEXT,        -- 切片原文
    embedding_json TEXT,    -- 向量（存文本，召回时反序列化算相似度）
    chunk_index INTEGER,    -- 第几片，用于溯源定位
    created_at TEXT)
```

### 面试价值 / 追问链（这条链是全场最深，务必全答）
- 对应面经题（真实出现）："混合检索策略""为什么要重排序""RAG 的切块规则，召回与重排序""FTS5 和 Embedding 两路分别检索，如何融合"
- 第 1 层：为什么加向量路？关键词检索不懂语义
- 第 2 层：向量怎么来？embedding 模型把文本映射成高维向量，语义相近距离近
- 第 3 层：切块策略、重叠窗口、两路怎么融合（RRF/加权）
- 第 4 层：为什么不用 ES/专门向量库？单机数据量下 SQLite+内存计算足够，免部署
- 第 5 层：召回不准怎么办？加 rerank 精排；改写 query 提升召回

---

## 七、P1-6：内容创作套件（统一 action 接口）

### 功能描述
一个 `POST /api/ai/action`，`action` 字段区分：润色、扩写、总结、翻译、生成大纲、生成标签。编辑器里选中一段文字就能调。**前端已有 CodeMirror，选中文本弹气泡是天然交互。**

### 技术方案
- **统一接口 + JSON 结构化输出**：每个 action 的 prompt 要求模型只返回 JSON（如润色返回 `{"result": "..."}`），后端解析 JSON 而不是靠正则抠文本。模型不支持 JSON mode 就靠 prompt 强约束 + 解析容错（去代码块围栏、兜底截取）。
- **Prompt 模板化管理**：action 的提示词抽成配置/常量表，不散落在代码里——面试考"Prompt 相关设计"时能讲：角色、任务、输入、约束、输出格式五要素（面经原题：优秀 Prompt 的核心组成）。
- 复用 P0-1 的消息组装与 P0-4 预算逻辑，action 是短上下文（无历史），实现成本低。

### 面试价值
- 对应面经题："项目中有没有用到 Prompt 相关设计？""System Prompt 是什么有什么要注意的？""JSON 约束输出"
- 能展示：结构化输出与容错、提示词工程化（不是手写一句咒语）、与编辑器真实集成的产品 sense。
- 注意：**别把这当核心卖点讲 3 分钟**，它是"工程化能力"的佐证，一条链 30 秒带过即可。

---

## 八、P1-7：自动标签 + 关联笔记推荐

### 功能描述
- 保存笔记时（或手动点"AI 整理"），把内容发给模型，让它从**已有 tags 表里选 3-5 个**最相关的（不存在就建议新的），写回 note_tags。
- "相关笔记推荐"：基于**已有 link_edges 双向链接图 + 内容相似度**（可复用 P1-5 的向量），在笔记页推"你可能还想看"。

### 为什么这个功能面试价值高
因为你**不是凭空做个玩具推荐**——你是在自己设计的 tags 表、link_edges 图上做 AI 增强。能讲清楚：
- 自动打标签**限定在已有标签集合里选**（而不是让模型自由造词）——否则标签体系会爆炸，这是真实工程决策；
- 关联推荐**图结构 + 向量双路**：链接是强信号（人工确认过相关），向量是弱信号（内容相似），融合排序。
- 对应面经题："知识梳理（标签/推荐）"是 AI 知识库类项目的经典考点。

### 表结构
无需新表：标签复用 `tags`/`note_tags`；推荐直接用 link_edges 反向查询 + P1-5 向量相似度算。**这正好体现"AI 长在已有架构上"。**

---

## 九、P2（可选加分，按时间决定）

1. **知识图谱问答**：你已有 KnowledgeGraph.vue + link_edges 图。进阶玩法：问"哪些笔记引用了这篇"是结构化查询（不用 AI，直接 SQL），但"总结这篇笔记被哪些主题的笔记引用、它们讲了什么"就是图+LLM 结合。面试讲"结构化数据 vs 非结构化各走各的路"很加分。
2. **function calling 轻量用法（不是硬塞 Agent）**：如"把这段翻译成英文并保存为新笔记"——模型先调 save 工具。只做 1-2 个真实工具即可，能讲工具调用规范与失败处理（面经考点"工具的调用规范""function calling"）。
3. **长文异步摘要**：导入的长文档，后台切块逐段总结再合成，前端给任务状态（进行中/完成）。涉及任务队列，是后端工程题的好素材。

---

## 十、落地排期（纯 API，按依赖顺序）

| 周 | 任务 | 依赖 |
|----|------|------|
| 1 | P0-1 多轮+会话表 ｜ P0-3.2 引用溯源升级 | 无，先打通"会话化 RAG" |
| 2 | P0-2 SSE 流式 ｜ P0-3.1 query 改写 ｜ P0-4 token 预算 | P0-1（流式要挂在会话上） |
| 3 | P1-5 混合检索（切块+embedding+RRF） | 无，可与 1、2 并行 |
| 4 | P1-6 内容创作 ｜ P1-7 标签/推荐 | 复用 chat 的消息与预算逻辑 |
| 5（可选） | P2 任选一两个 | P1-5（图谱问答/向量推荐依赖它） |

做完 P0+P1（约 4 周），项目 AI 能力从"单轮 FTS5 问答"变成：
**多轮会话 + 流式 + 改写 + 混合检索 + 重排 + 溯源 + 拒答 + 创作 + 知识梳理**——这套东西面试官深挖任意一条你都答得到第 3-4 层。

---

## 十一、简历/面试口径（2026-09-07 P0+P1+P2 全部完成后已升级，RESUME.md 已按此更新）

现在 RESUME.md 里 AI 相关没写具体能力。按"已实现"原则，P0+P1 完成后可升级为：

> AI 层基于会话式 RAG：FTS5 关键词与向量 embedding 混合检索 + RRF 融合，Query 改写提升召回，回答带引用溯源与显式拒答；支持 SSE 流式输出、多轮上下文 token 预算管理；提供润色/扩写/总结等结构化创作接口与基于标签体系与双向链接图的自动整理能力。全部对接 OpenAI 兼容在线 API。

**红线**：没实现的别写"已支持"。每完成一项，同步更新本文档状态，面试前把【面试价值】段过一遍。

---

## 十二、技术栈影响评估（2026-09-05 补充：零新增依赖 + 三个决策点）

**现状盘点（已核对代码）**：后端 = Crow + nlohmann/json + **WinHTTP**（ai_routes.h 用 Windows 原生 HTTP 调 API）；数据层 SQLite（database.cpp 集中建表）；前端 Vue3 + TS + marked + d3，**无 pinia / 无 axios**。

**核心原则**：本方案所有 AI 功能本质只有三件事——HTTP POST 调 API、JSON 解析、SQLite 读写，三样现有代码全有，**不引入任何重量级依赖**。逐项对照：

| 功能 | 实现方式 | 新增依赖 |
|---|---|---|
| 多轮会话/会话表 | SQLite 新表，复用现有封装 | 无 |
| query 改写 / 内容创作 / 标签推荐 | chat API 的 prompt 变体，复用 WinHTTP 调用 | 无 |
| embedding / rerank | 同一套 WinHTTP POST，只换 URL 与 body | 无 |
| 向量存储 | SQLite 文本列存 JSON（note_chunks） | 无 |
| 余弦相似度 / RRF 融合 | 纯数学手写（几十行） | 无 |
| SSE 消费（前端） | fetch + ReadableStream（EventSource 不支持 POST） | 无 |
| 会话状态管理（前端） | 手写 reactive store 或引入 pinia（极轻，二选一） | pinia（可选） |
| Markdown / 图谱展示 | 已有 marked / d3 | 无 |

**刻意不引入（面试口径 = 亮点）**：LangChain/LlamaIndex（Python 生态，本 C++ 项目不用；RAG pipeline 手写串联本身是加分叙事）、独立向量库服务（Milvus/Chroma/Qdrant）、Elasticsearch、本地模型、UI 组件库。

**三个决策点（动工前逐个拍板）**：
1. **SSE 服务端实现**：Crow 对长连接流式响应不算原生友好。路径 a 手写 chunked 写流（需处理客户端中途断开）；路径 b 前端轮询（不推荐，体验差）。这是 P0 唯一有真技术含量的点，让 AI 先给方案并说明断连处理再动手。
2. **token 计数**：最准 = tiktoken C++ 移植（新增依赖）；最省 = 字符粗估 + 用 API 返回的 usage 校准。个人项目推荐**先粗估后校准**，能讲出"为什么不用精确 tokenizer"的权衡。
3. **sqlite-vec**：现在不引入；到十万级 chunk 再考虑，届时是 SQLite 生态的扩展文件，仍不算重依赖。

**面试口径一句话**：AI 层零重量级依赖，RAG 检索、向量计算、流式输出、上下文管理全部基于 C++ 原生能力与 SQLite 手写实现，对外仅依赖 OpenAI 兼容 HTTP API。

---

*本文档基于代码现状核对（ai_routes.h / database.cpp / AIChatPanel.vue / MindVault-AI架构.md），功能均可落到现有 C++/Vue 架构。*

---

## 十三、已实现记录（P0-1 / P0-2 / P0-3.1 / P0-4，实现日期 2026-09-05）

### 13.1 本批改动文件清单

| 文件 | 改动类型 | 改动内容 |
|------|----------|----------|
| `third_party/crow_all.h` | 修改 | response 类新增 public 字段 `stream_sink_` / `stream_close_`（std::function），connection 建立响应时绑定为 asio::write 直写 socket / shutdown_write+close；lambda 内加 try/catch 防异常崩溃 |
| `src/database.h` | 修改 | 新增 `UpdateAIMessageTokens(int64_t message_id, int tokens)` 声明 |
| `src/database.cpp` | 修改 | 实现 `UpdateAIMessageTokens`：`UPDATE ai_messages SET tokens = ? WHERE id = ?`（参数化） |
| `src/routes/ai_routes.h` | 修改 | 新增：`QueryRewrite` 函数、`HttpPostStream`（WinHTTP 流式读取）、`SSEParser`（解析上游 SSE chunk）、`CallOnlineAPIStream`（流式调用+delta回调+abort检查）、`/api/ai/chat/stream` 端点；`/api/ai/chat` 接入 QueryRewrite；`buildHistory` 接入 token 预算截断；usage 字段校准 completion_tokens；SSE 端点全 try/catch + streamStarted 标志 + search_svc 值捕获 |
| `web/src/api.ts` | 修改 | 新增 `StreamCallbacks` 接口（onDelta/onDone/onError）+ `chatStream` 函数（fetch + ReadableStream + getReader + SSE 解析 + AbortSignal 取消） |
| `web/src/components/AIChatPanel.vue` | 修改 | script：sendMessage 重写为流式版本，新增 isStreaming/streamError/abortController/lastQuestion 状态，cancelStream/retryLast 函数；template：流式光标、取消按钮、错误提示条+重试按钮；CSS：.stream-cursor 动画、.stream-error-bar、.btn-retry、.btn-cancel |
| `CLAUDE.md` | 修改 | 当前进度段落更新 P0-2/P0-3.1/P0-4 完成状态 |

**数据库迁移**：无需迁移。`ai_messages.tokens` 列在 P0-1 建表时已存在（`CREATE TABLE IF NOT EXISTS` 幂等），`UpdateAIMessageTokens` 只是新增了对该列的 UPDATE 操作，老库文件不受影响。

### 13.2 新增 API：POST /api/ai/chat/stream（SSE 流式）

**请求**：
```json
POST /api/ai/chat/stream
Content-Type: application/json
Authorization: Bearer <token>

{
  "conversation_id": 1,
  "question": "什么是 RAG？",
  "provider": "openai",
  "model": "gpt-4o-mini",
  "api_key": "sk-xxx",
  "api_url": ""
}
```

**响应**（`Content-Type: text/event-stream`，逐块推送）：
```
data: {"type":"start","conversation_id":1}

data: {"type":"delta","content":"检索"}

data: {"type":"delta","content":"增强"}

data: {"type":"delta","content":"生成"}

data: {"type":"done","message_id":42,"tokens":156}
```

**错误事件**（API Key 未配置或调用失败时）：
```
data: {"type":"start","conversation_id":1}

data: {"type":"error","message":"请配置 API Key（流式暂不支持 ollama）"}

data: {"type":"done","message_id":0,"tokens":0}
```

**SSE 事件类型说明**：
| type | 含义 | 字段 |
|------|------|------|
| `start` | 流开始，会话已建立 | conversation_id |
| `delta` | 增量文本片段 | content |
| `error` | 上游 API 错误或网络异常 | message |
| `done` | 流结束，消息已入库 | message_id, tokens |

**客户端断开处理**：后端 `stream_sink_` lambda 内 try/catch 捕获 asio::write 异常，置 `aborted=true` 原子标志，后续写入全部跳过；DB 事务正常 Commit（用户消息已写入），上游 API 请求仍会跑完（无法中断第三方 API），但不再推给客户端。

### 13.3 已有 API 扩展：POST /api/ai/chat（接入 QueryRewrite + token 校准）

**请求**（不变，兼容 P0-1）：
```json
POST /api/ai/chat
{
  "conversation_id": 1,
  "question": "它和微调有什么区别",
  "provider": "openai",
  "model": "gpt-4o-mini",
  "api_key": "sk-xxx"
}
```

**响应**（不变）：
```json
{
  "ok": true,
  "data": {
    "answer": "RAG（检索增强生成）和微调的区别在于...",
    "conversation_id": 1,
    "message_id": 5,
    "model": "gpt-4o-mini",
    "provider": "openai",
    "sources": []
  }
}
```

**内部新增流程**（对调用方透明）：
1. 写入用户消息后，调用 `QueryRewrite`：取最近 2 轮（4条）历史 + 当前问题，构造改写 prompt 调用模型，输出无指代独立问题；历史不足 2 轮或改写失败时降级返回原问题
2. 改写结果与原问题不同时，用改写后的 query 重新 FTS5 检索
3. `buildHistory` 按 token 预算从最老消息开始截断（系统提示词+当前问题+检索上下文永远保留）
4. 调用模型后，从响应 `usage.completion_tokens` 提取精确 token 数，调用 `UpdateAIMessageTokens` 校准 assistant 消息的 tokens 列（替代字符粗估值）

### 13.4 关键常量（ai_routes.h）

| 常量 | 值 | 用途 |
|------|-----|------|
| `MAX_HISTORY_TURNS` | 10 | 取历史消息最大轮数（20条） |
| `MAX_CONTEXT_TOKENS` | 8000 | 上下文 token 预算上限 |
| `SYSTEM_PROMPT_TOKENS` | 200 | 系统提示词预留 token |
| `estimateTokens(s)` | `s.length()/3 + 1` | 字符粗估 token（中文约 1 字≈0.6-0.7 token，取保守值 1/3） |

### 13.5 验收测试结果（2026-09-06 自测）

- 功能主链路：非流式 /api/ai/chat 返回 200 + answer；流式 /api/ai/chat/stream 返回 200 + text/event-stream + start/error/done 三事件
- SQL 注入：本批所有新写 SQL 全部参数化，零字符串拼接
- 越权：所有会话/消息查询经 `GetAIConversation(convId, uid)` 校验归属（SQL `WHERE id=? AND user_id=?`），不可能查到别的用户数据
- 边界：空 question→400；超长 10000 字符→200；异常 conversation_id→404；连续 3 条→[200,200,200]
- 兼容回归：P0-1 会话 CRUD 全部正常，旧端点 /api/ai/chat 兼容
- 前端：fetch+ReadableStream 逐块渲染，错误提示条+重试按钮，取消按钮，v-if 防白屏
- 遗留：API Key 未配置时流式返回 error 事件（需配置 OpenAI 兼容 API Key 验证实际 delta 流式输出）；非流式未登录返回 404 而非 401（P0-1 遗留）


---

## 十四、P1-5 混合检索已实现记录（实现日期 2026-09-07）

### 14.1 本批改动文件清单

| 文件 | 改动类型 | 改动内容 |
|------|----------|----------|
| `src/database.h` | 修改 | 新增 `AddChunk`/`GetChunksByNote`/`DeleteChunksByNote`/`GetAllChunks` 四个方法声明 |
| `src/database.cpp` | 修改 | `Init()` 新增 `note_chunks` 表 + 索引（`CREATE TABLE IF NOT EXISTS` 幂等）；实现四个 chunk CRUD 方法，全部参数化 |
| `src/services/chunk_service.h` | **新增** | 切块逻辑（标题→段落→800字符窗口+50字重叠+句子边界对齐）、`EmbedText`（WinHTTP 3秒超时）、余弦相似度、`VectorSearch`（内存算余弦取topN）、`RebuildChunksForNote`（删旧→切块→存库→调embedding） |
| `src/services/search_service.h` | 修改 | 新增 `HybridSearch` 方法：FTS5路 + 向量路 + RRF融合（k=60）+ embedding失败降级纯FTS5 + 日志；输出格式与`Search`完全一致 |
| `src/routes/search_routes.h` | 修改 | `/api/search` 内部改用 `HybridSearch`，支持可选 `api_key`/`api_url`/`model` query参数；新增2000字符query长度限制 |
| `src/routes/ai_routes.h` | 修改 | 非流式 + 流式两个端点的 RAG 检索都改用 `HybridSearch`（含 QueryRewrite 后重新检索） |
| `src/services/note_service.h` | 修改 | `Create`/`Update` 增加可选 API 配置参数，保存后自动 `RebuildChunksForNote`（try/catch包裹，失败不影响保存） |
| `src/routes/note_routes.h` | 修改 | POST/PUT 笔记从请求体读取可选 `api_url`/`api_key`/`model` 传给 service |

**数据库迁移**：`note_chunks` 表用 `CREATE TABLE IF NOT EXISTS`，幂等，老库启动时自动建表，不影响已有数据。无需删旧库、无需 ALTER TABLE、无需数据迁移。老笔记无 chunk，需手动重新保存或后续加批量重建接口。

### 14.2 API 变更（请求/响应格式兼容，前端零改动）

#### GET /api/search（内部升级，格式不变）

**请求**（新增可选参数）：
```
GET /api/search?q=process&api_key=sk-xxx&api_url=https://api.openai.com/v1/embeddings&model=text-embedding-3-small
```

| 参数 | 必填 | 说明 |
|------|------|------|
| q | 是 | 搜索关键词，最大2000字符 |
| api_key | 否 | embedding API key，为空时跳过向量路，纯FTS5 |
| api_url | 否 | embedding API URL |
| model | 否 | embedding 模型名 |

**响应**（格式不变，与纯FTS5完全一致）：
```json
{
  "ok": true,
  "data": [
    {"id": 4, "title": "OS Notes", "folder": "default", "updated_at": "...",
     "title_highlight": "OS Notes", "content_highlight": ">>>Process<<< is the minimal unit..."}
  ]
}
```

**降级行为**：api_key 为空 / embedding API 失败 / 超时 / 返回格式错误 → 向量路跳过 → 直接返回 FTS5 结果，用户无感知，后端日志打印 `[HybridSearch] embedding failed, fallback to FTS5 only`。

#### POST /api/notes（新增可选参数，格式不变）

**请求**（新增可选参数）：
```json
{
  "title": "OS Notes",
  "content": "...",
  "folder": "default",
  "api_url": "https://api.openai.com/v1/embeddings",
  "api_key": "sk-xxx",
  "model": "text-embedding-3-small"
}
```

有 API 配置时：保存笔记后自动切块 + 调 embedding + 存 `note_chunks` 表（3秒超时，失败只存文本不存向量）。
无 API 配置时：只切块存文本，`embedding_json` 为 NULL，检索时跳过该 chunk。

**响应**：不变，201 + 笔记详情。

### 14.3 关键常量

| 常量 | 值 | 位置 | 用途 |
|------|-----|------|------|
| `CHUNK_MAX_SIZE` | 800 | chunk_service.h | 单块最大字符数 |
| `CHUNK_OVERLAP` | 50 | chunk_service.h | 相邻块重叠字符数（防切断语义） |
| `CHUNK_MIN_SIZE` | 50 | chunk_service.h | 少于此长度的块跳过 |
| `EMBEDDING_TIMEOUT_MS` | 3000 | chunk_service.h | embedding API 超时（毫秒） |
| `RRF_K` | 60 | search_service.h | RRF 融合 k 值 |
| query 长度限制 | 2000 | search_routes.h | 搜索 query 最大字符数（防超长URL连接重置） |

### 14.4 切块规则（明确）

1. 标题拼到内容前面（`# 标题

内容`），保证 embedding 包含上下文
2. 按 Markdown 标题（`#`/`##`/`###`）切分
3. 没有标题的按空行段落切
4. 单段超 800 字符按 800 窗口切，相邻块重叠 50 字符
5. 窗口切分时从切分点往回找最近的句子结束符（。！？.!?），在句子边界切，不切断完整句子
6. 单块少于 50 字符跳过

### 14.5 RRF 融合算法

```
score(note_id) = Σ 1 / (60 + rank)
```
- FTS5 路和向量路各自排序，rank 从 1 开始
- 两路都命中的笔记，分数是两路 rank 分数之和
- 按融合分降序取 topN
- **不做分数归一化**：FTS5 的 bm25 分数和余弦相似度量纲完全不同，归一化很难做对；RRF 只看排名不看分数，天然消除量纲差异

### 14.6 验收测试结果（2026-09-07 自测）

- 功能主链路：创建笔记→搜索"process"返回1条正确命中（highlight正常）；搜索"TCP"返回2条
- 切块验证：笔记被切成4个chunk（97/96/89/109 chars），无API key时embedding全部为NULL
- embedding降级：假API key搜索返回200+1条结果（降级FTS5），不报错
- RRF：代码中 RRF_K=60，融合公式 1.0/(RRF_K+rank)，无归一化
- 向量存储：embedding_json 为 TEXT 类型，未引入 sqlite-vec 扩展
- 零依赖：复用 WinHTTP，无 LangChain/向量数据库
- 边界：空q→400；超长3000字符→400（不崩溃）；异常id→404；重复创建→201/201
- 兼容回归：P0会话功能全部正常（创建会话/列表/聊天/详情）
- 前端：零改动，HybridSearch输出格式与Search一致，不会白屏
- 已知问题：FTS5默认unicode61分词器不识别中文（非本批引入）；老笔记无chunk需手动重建；无真实API key无法端到端测向量路


---

## 十五、P1-6 内容创作 + P1-7 自动标签/关联推荐 已实现记录（实现日期 2026-09-07）

### 15.1 本批改动文件清单

| 文件 | 改动类型 | 改动内容 |
|------|----------|----------|
| `src/services/ai_action_service.h` | **新增** | ActionPrompt 模板常量表（6个action五要素prompt）+ JSON提取容错（去代码块围栏+截花括号+重试一次+降级纯文本）+ ExecuteAction 统一入口 + AutoTag（只从已有标签集选+建议新标签不入库） |
| `src/routes/ai_routes.h` | 修改 | 新增 `POST /api/ai/action` 端点（action白名单校验+参数校验+AICaller lambda复用CallOnlineAPIWithMessages+try/catch） |
| `src/services/note_service.h` | 修改 | 新增 `GetRecommendations(note_id, limit)`：link_edges双向链接强信号(+2.0) + P1-5向量余弦弱信号(0~1.0) 加权融合，向量路try/catch失败不影响强信号路 |
| `src/routes/note_routes.h` | 修改 | 新增 `GET /api/notes/:id/recommendations` 端点（笔记存在性校验+404） |
| `web/src/api.ts` | 修改 | 新增 `aiAction()` / `getRecommendations()` 函数 + `AIActionResult`/`Recommendation`/`AIApiConfig` 类型 |
| `web/src/components/Editor.vue` | 修改 | Markdown工具栏新增"✨ AI"按钮 + AI创作面板（6个action按钮+结果展示+替换原文/复制+降级warning+错误提示+加载状态） |
| `web/src/components/BacklinksPanel.vue` | 修改 | 新增"推荐"tab（link_edges强信号🔗 + 向量弱信号✨ + 两者🔗✨，相关度百分比展示） |

**数据库迁移**：零迁移。复用 `tags`/`note_tags`/`link_edges`/`note_chunks` 四张现有表，不加新表，老库启动不崩。

### 15.2 新增 API 请求响应示例

#### POST /api/ai/action（内容创作统一接口）

**请求**：
```json
{
  "action": "polish",
  "text": "这段文字写的不太好需要润色",
  "api_url": "https://api.openai.com/v1/chat/completions",
  "api_key": "sk-xxx",
  "model": "gpt-4o-mini",
  "target_lang": "en",
  "note_id": 1
}
```

| 参数 | 必填 | 说明 |
|------|------|------|
| action | 是 | polish/expand/summarize/translate/outline/tags |
| text | 是 | 选中的文本，最大无硬限制（受API max_tokens约束） |
| api_url/api_key/model | 是 | AI 配置（从前端 localStorage 读取） |
| target_lang | 否 | 仅 translate 用，默认"英文" |
| note_id | 否 | 仅 tags 用，写入 note_tags |

**响应（成功）**：
```json
{
  "ok": true,
  "data": {
    "action": "polish",
    "result": "润色后的文本",
    "degraded": false
  }
}
```

**响应（解析失败降级）**：
```json
{
  "ok": true,
  "data": {
    "action": "polish",
    "result": "模型原始输出文本",
    "degraded": true,
    "warning": "模型未返回结构化JSON，已降级为纯文本"
  }
}
```

**响应（action=tags 自动标签）**：
```json
{
  "ok": true,
  "data": {
    "action": "tags",
    "degraded": false,
    "selected_tags": ["编程", "后端"],
    "suggested_new_tags": ["RAG"]
  }
}
```
- `selected_tags`：只保留在 tags 表中存在的标签，已自动写入 note_tags
- `suggested_new_tags`：AI 建议的新标签，**不入库**，等用户确认后手动添加

#### GET /api/notes/:id/recommendations（关联笔记推荐）

**请求**：`GET /api/notes/1/recommendations`

**响应**：
```json
{
  "ok": true,
  "data": [
    {"id": 2, "title": "Java 基础", "folder": "default", "score": 2.8, "source": "both"},
    {"id": 3, "title": "C++ 基础", "folder": "default", "score": 2.0, "source": "linked"},
    {"id": 5, "title": "编程语言对比", "folder": "default", "score": 0.85, "source": "similar"}
  ]
}
```

| 字段 | 说明 |
|------|------|
| score | 融合分数 = (link_edges命中?2.0:0) + 向量余弦相似度(0~1.0) |
| source | both=链接+向量都命中 / linked=只有链接 / similar=只有向量 |

### 15.3 关键设计决策

| 决策 | 选择 | 原因 |
|------|------|------|
| action 接口统一 | 一个 POST /api/ai/action 覆盖6种操作 | 前端一个函数调所有action，不用为每个action写端点 |
| JSON 容错策略 | 去围栏→截花括号→重试一次→降级纯文本 | 模型不保证严格JSON，必须容错否则前端崩 |
| 短上下文无历史 | messages只有system+user两条 | 内容创作是独立操作，带历史会被之前对话带偏；省token响应快 |
| 自动标签过滤 | selected_tags用std::find只保留已有标签 | 不许AI发明新标签，否则标签体系爆炸 |
| 建议新标签不入库 | suggested_new_tags原样返回 | 等用户确认，避免污染标签体系 |
| 推荐融合算法 | score=(linked?2.0:0)+similarity | link_edges是人工确认的强关联(+2.0)，向量是算法推测的弱关联(0~1.0)，强信号权重远大于弱信号 |
| 向量路降级 | try/catch包裹，失败不影响强信号路 | 老笔记无embedding时推荐退化为纯link_edges，不报错不白屏 |

### 15.4 验收测试结果（2026-09-07 自测）

- 功能主链路：更新笔记建link_edges → 推荐返回2条强信号笔记(score=2.0, source=linked)；action接口假key降级不崩
- 约束1 JSON容错：假key返回degraded:true+warning，接口不崩
- 约束2 短上下文：代码审查messages只有system+user两条，无历史
- 约束3 标签过滤：代码审查std::find过滤只保留已有标签，建议标签不入库
- 约束4 推荐融合：score=2.0强信号排最前，代码公式加权正确
- 约束5 不加新表：零CREATE TABLE，零迁移
- 边界：空action/text→400；超长50000字符→200降级不崩；异常id→404；重复3次→全部200
- 兼容回归：会话创建/列表/聊天/详情全部正常；P1-5搜索正常
- 前端：vue-tsc零错误；AI面板6按钮+替换/复制+降级warning+错误提示；推荐tab展示source图标+相关度
- 已知限制：无真实API key无法测JSON解析成功路径和标签选择效果；老笔记无embedding向量路暂无法端到端测（P1-5已知问题）


---

## 十六、P2 图谱问答 + function calling 已实现记录（实现日期 2026-09-07）

### 16.1 本批改动文件清单

| 文件 | 改动类型 | 改动内容 |
|------|----------|----------|
| `src/services/graph_qa_service.h` | **新增** | 图谱问答服务：关键词正则路由判定（结构化/非结构化，非结构化优先级高）+ 结构化路纯SQL查link_edges（不调AI）+ 非结构化路取关联笔记摘要调LLM + ExtractNoteName从问题中提取笔记名（FTS5匹配） |
| `src/services/function_calling_service.h` | **新增** | function calling服务：2个真实只读工具（search_notes→SearchService::Search，get_note→NoteService::GetById）+ 单轮tool loop（第一次调AI带tools→执行工具→第二次调AI）+ 三层降级（无tool_calls直接返回/解析失败降级/整体异常降级） |
| `src/routes/ai_routes.h` | 修改 | 新增 `CallOnlineAPIWithTools`（带tools参数的AI调用）+ `POST /api/ai/graph-qa` + `POST /api/ai/chat-with-tools` 两个端点 |
| `web/src/api.ts` | 修改 | 新增 `graphQA()`/`chatWithTools()` 函数 + `GraphQAResult`/`ToolCall`/`ChatWithToolsResult` 类型 |
| `web/src/components/KnowledgeGraph.vue` | 修改 | 图谱弹窗底部新增问答输入框：结构化问题高亮路由标签+返回笔记列表可点击、非结构化问题展示AI回答+引用来源、降级黄色标记 |
| `web/src/components/AIChatPanel.vue` | 修改 | 新增"🔧 工具模式"开关（默认关闭）+ 工具调用过程展示（工具名/状态✓✗/参数）+ 降级提示 |

**数据库迁移**：零迁移。复用 `link_edges`/`notes`/`notes_fts` 三张现有表，不加新表，老库启动不崩。

### 16.2 新增 API 请求响应示例

#### POST /api/ai/graph-qa（知识图谱问答）

**请求**：
```json
{
  "question": "Python 基础链接到哪些笔记？",
  "note_id": 1,
  "api_url": "https://api.openai.com/v1/chat/completions",
  "api_key": "sk-xxx",
  "model": "gpt-4o-mini"
}
```

| 参数 | 必填 | 说明 |
|------|------|------|
| question | 是 | 自然语言问题 |
| note_id | 否 | 指定上下文笔记，不传则从问题中提取笔记名FTS5匹配 |
| api_url/api_key/model | 否 | 结构化路不需要，非结构化路需要 |

**响应（结构化路）**：
```json
{
  "ok": true,
  "data": {
    "route": "structured",
    "direction": "forward",
    "note_id": 1,
    "notes": [
      {"id": 2, "title": "Java 基础", "updated_at": "2026-09-07 12:00:00"},
      {"id": 3, "title": "C++ 基础", "updated_at": "2026-09-07 12:00:00"}
    ]
  }
}
```

**响应（非结构化路）**：
```json
{
  "ok": true,
  "data": {
    "route": "unstructured",
    "answer": "Python基础被Java和C++两篇笔记引用，它们都...",
    "sources": [{"id": 2, "title": "Java 基础"}, {"id": 3, "title": "C++ 基础"}]
  }
}
```

**路由判定规则**：
- 非结构化关键词（优先级高）：总结/解释/为什么/对比/讲了什么/区别/关系/分析/概括/描述/说明/怎么样/如何/评价 → 走 LLM
- 结构化关键词：哪些/谁/几个/引用了/被引用/链接/关联/列出/有哪些 → 走 SQL
- 默认走非结构化（更安全）

#### POST /api/ai/chat-with-tools（function calling）

**请求**：
```json
{
  "question": "帮我找关于 RAG 的笔记",
  "api_url": "https://api.openai.com/v1/chat/completions",
  "api_key": "sk-xxx",
  "model": "gpt-4o-mini"
}
```

**响应**：
```json
{
  "ok": true,
  "data": {
    "answer": "找到一篇关于RAG的笔记：RAG入门，主要讲了检索增强生成的原理...",
    "tool_calls": [
      {
        "name": "search_notes",
        "args": "{\"query\": \"RAG\"}",
        "result": "[{\"id\":5,\"title\":\"RAG入门\",\"snippet\":\"...\"}]",
        "status": "success"
      }
    ],
    "degraded": false
  }
}
```

**工具定义**（OpenAI 兼容格式）：
| 工具名 | 对应代码函数 | 功能 | 参数 |
|--------|-------------|------|------|
| search_notes | SearchService::Search | 按关键词搜索笔记 | query: string |
| get_note | NoteService::GetById | 获取笔记完整内容 | note_id: integer |

### 16.3 关键设计决策

| 决策 | 选择 | 原因 |
|------|------|------|
| 路由判定用关键词正则，不用AI | 非结构化关键词优先级高，默认走非结构化 | 省一次API调用、延迟更低；误判代价只是多调一次AI |
| 结构化路不调AI | 纯SQL查link_edges，毫秒级响应 | 结构化问题（"哪些笔记引用了X"）不需要AI，直接查图更快 |
| function calling只做单轮tool loop | 第一次调AI(带tools)→执行工具→第二次调AI(不带tools) | 多轮tool loop容易死循环，单轮足够展示核心概念 |
| 工具只做只读 | search_notes + get_note，不做创建/删除 | 写操作风险高，本批定位是"轻量用法"不是Agent |
| 工具结果截断 | 搜索最多5条每条前200字，笔记内容最多1000字 | 防止第二次AI调用prompt超长 |
| 三层降级 | 无tool_calls直接返回 / 解析失败降级 / 整体异常降级 | 模型不支持tools时退化为普通问答，不白屏 |

### 16.4 验收测试结果（2026-09-07 自测）

- 功能主链路：结构化路正向查询返回2条关联笔记(route=structured)；反向查询正确返回空+warning；非结构化路假key降级不崩；function calling假key降级不崩
- 约束1 路由判定：结构化问题→route=structured(纯SQL)，非结构化问题→route=unstructured(调LLM)
- 约束2 不引图数据库：复用link_edges表，零新表
- 约束3 真实工具：search_notes→SearchService::Search，get_note→NoteService::GetById
- 约束4 工具失败处理：ExecuteTool内try/catch，整体异常降级
- 约束5 零依赖：纯C++17+SQLite+WinHTTP
- 边界：空question→400；无API key→400；笔记名找不到→warning；未登录→401
- 兼容回归：老聊天/笔记列表/搜索/推荐全部正常
- 前端：vue-tsc零错误
- 发现并修复bug：ExtractNoteName死循环（remove_words含空格导致find总能命中replace无变化），修复后结构化路响应从假死超时降到毫秒级
- 已知限制：无真实API key无法测非结构化路AI回答质量和function calling工具实际调用效果

### 16.5 面试核心难点

**图谱问答的"结构化 vs 非结构化"分路路由**：用关键词正则做路由判定（非结构化优先级高），结构化路纯SQL查link_edges不调AI（毫秒级），非结构化路把关联笔记摘要作为上下文喂给LLM。实现中遇到ExtractNoteName死循环bug（remove_words列表含空格导致find总能命中、replace无变化），修复后结构化路从假死超时恢复正常。这个设计的面试亮点是"结构化数据走SQL、非结构化走LLM"的分路思想，以及"不用AI做路由、用关键词正则省一次调用"的工程权衡。

## 十七、P0-3.2 引用溯源已实现记录（实现日期 2026-09-07）

### 17.1 本批改动文件清单
- 后端 src/routes/ai_routes.h：/api/ai/chat/stream 的 done 事件新增携带 sources（检索结果数组，含 id/title/folder/title_highlight/content_highlight），与 /api/ai/chat 非流式响应对齐
- 前端 web/src/api.ts：ChatMessage 增加可选 sources 字段；StreamCallbacks.onDone 增加第三参 sources；SSE 解析 done 事件透传
- 前端 web/src/components/AIChatPanel.vue：assistant 消息下方渲染「📎 参考来源」chips（标题可点击）；新增 selectNote emit
- 前端 web/src/App.vue：AIChatPanel 接入 @select-note → selectNote（点击来源跳转打开笔记）
- 顺手修复两个存量前端 bug：api.ts getRecommendations URL 模板字符串丢反引号（请求路径错误）、BacklinksPanel.vue v-else-if 误接 v-else 后（Vue 语法错误致 vite build 失败）

### 17.2 验证结果（2026-09-07 自测）
- 前端 vue-tsc -b 类型检查通过；npm run build 通过（修复前 vite build 失败）
- 后端 MSVC Release 编译通过
