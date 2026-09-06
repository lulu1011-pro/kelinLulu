# CLAUDE.md — MindVault 项目全貌

> 本文件供 AI 编程助手(Claude Code / Codex / Cursor / Trae 等)自动加载。新会话先读本文件 + CONTEXT.md,再动手。

## 项目是什么
MindVault:本地优先的个人知识库/Wiki 应用(类 Obsidian/Notion 的轻量版),核心理念是"用 Wiki 链接连接笔记"。C++17 后端 REST API + Vue3 前端,前后端分离,浏览器访问。

## 技术栈
- 后端:C++17 / Crow(header-only HTTP 框架)/ SQLite3 + FTS5(WAL 模式)/ nlohmann::json;MSVC VS2022 + CMake
- 前端:Vue3 + Vite + TypeScript / CodeMirror 6 编辑器 / marked 渲染 Markdown;不用 Pinia(状态集中在 App.vue,props/emit 传递)
- 运行:后端 127.0.0.1:8080;前端 npm run dev,Vite 代理 /api → 8080

## 目录速览
- src/ 后端:main.cpp 入口;database.h/cpp(SQLite 封装,参数化查询、UTF-16 路径、WAL);models/(note/tag);services/(note/search/tag/flashcard/user 等,service 层全部 header-only 写在 .h);routes/(note/search/file/ai/share/user/flashcard/collab/perm,统一 RegisterXxxRoutes(crow::App<>&, Database&) 模式);utils/(config 单例、response 统一响应)
- web/ 前端:api.ts 封装全部后端 API;components/(Editor/Sidebar/AIChatPanel/TabBar 等)
- 根目录设计文档:P0-1_DESIGN.md、MindVault-AI功能增强设计_20260905.md、MindVault-AI架构.md

## 数据库(实际表,以 database.cpp 为准)
users、notes、tags、note_tags、link_edges、versions、flashcards、permissions、ai_conversations、ai_messages;另 notes_fts 为 FTS5 虚拟表(触发器同步)。CONTEXT.md 里的旧表清单已过时,以此为准。

## API 约定
统一响应 {"ok":true,"data":...} / {"ok":false,"error":{code,message}};一律走 utils::Success/Error,不手拼 JSON。

## 当前进度(重要,先读这里)
- 多用户体系已上线(2026-09-05 提交):注册/登录鉴权,后端按 token 解析 user_id,每个用户独立 SQLite 库,所有查询带用户条件。
- AI 增强 P0-1「多轮会话记忆」已完成并提交(2026-09-05,提交 2c0fa09 建表→72dc21b 会话 API→39c6b35 前端列表→e1f03f6 修 TS→89c6258 review):ai_conversations/ai_messages 表已建;会话 CRUD 全套;ai_routes.h 已有会话级互斥锁、MAX_HISTORY_TURNS=10、MAX_CONTEXT_TOKENS=8000、system prompt 截断;/api/ai/chat 兼容单轮(无 conversation_id)与会话模式。
- AI 增强 P0-2「SSE 流式输出」已完成(2026-09-06):crow_all.h 打补丁新增 response.stream_sink_/stream_close_ 直写 socket;新增 HttpPostStream + SSEParser + CallOnlineAPIStream 流式基础设施;新增 /api/ai/chat/stream 端点(手动写 HTTP 头、逐块推 SSE、客户端断开检测、token 校准);前端 api.ts 新增 chatStream(fetch + ReadableStream + SSE 解析 + AbortController 取消);AIChatPanel 接入流式渲染(逐字输出+闪烁光标+取消按钮+错误提示+重试)。原有 /api/ai/chat 非流式端点保持兼容。
- AI 增强 P0-3.1「Query 改写/指代消解」已完成(2026-09-06):新增 QueryRewrite 函数(取最近 2 轮历史 + 当前问题,调用模型改写为无指代独立问题,失败降级返回原问题);已接入 /api/ai/chat 和 /api/ai/chat/stream 端点,改写后重新检索。验证场景:先问「什么是 RAG」→「它和微调有什么区别」→「那我项目里现在用的是哪种」,第三句可正确消解指代。
- AI 增强 P0-4「Token 预算」已完成(2026-09-06):buildHistory 已做从最老开始丢的 token 预算截断(系统提示词和当前问题永远保留);新增 usage 字段校准(API 返回 completion_tokens 后调用 UpdateAIMessageTokens 校准 assistant 消息 tokens);估算用字符粗估(length/3+1),再用 API usage 校准。不用精确 tokenizer 的原因:1)引入额外依赖增加体积 2)不同模型 tokenizer 不同 3)usage 字段是模型侧精确统计。
- AI 增强 P1-5「混合检索(FTS5+向量+RRF)」已完成(2026-09-07):新增 note_chunks 表(切块+embedding_json,CREATE TABLE IF NOT EXISTS 幂等不影响老库);新增 chunk_service.h(切块规则:标题→段落→800字符窗口+50字重叠+句子边界对齐;EmbedText 复用 WinHTTP 3秒超时;余弦相似度内存计算;VectorSearch);SearchService 新增 HybridSearch(FTS5路+向量路+RRF融合 k=60,embedding失败降级纯FTS5+日志,输出格式与Search完全一致);/api/search 和 AI对话RAG检索接入 HybridSearch;笔记保存后自动 RebuildChunksForNote(try/catch包裹,失败不影响保存);搜索query加2000字符长度限制(防超长URL连接重置)。前端零改动。已知问题:FTS5默认unicode61分词器不识别中文(非本批引入);老笔记无chunk需手动重建。
- AI 增强 P1-6「内容创作套件」已完成(2026-09-07):新增 ai_action_service.h(ActionPrompt模板常量表6个action+JSON提取容错去代码块围栏/截花括号/重试一次/降级纯文本+ExecuteAction统一入口);新增 POST /api/ai/action 端点(polish/expand/summarize/translate/outline/tags,短上下文无历史);前端 Editor.vue 新增AI创作面板(6按钮+结果展示+替换原文/复制+降级warning+错误提示)。
- AI 增强 P1-7「自动标签+关联推荐」已完成(2026-09-07):AutoTag 只从已有标签集选(std::find过滤),建议新标签单独字段不入库;NoteService 新增 GetRecommendations(link_edges双向链接强信号+2.0与P1-5向量余弦弱信号0~1.0加权融合,向量路try/catch失败不影响强信号路);新增 GET /api/notes/:id/recommendations 端点;前端 BacklinksPanel.vue 新增推荐tab(source图标+相关度百分比)。零新表零迁移,复用tags/note_tags/link_edges/note_chunks。
- P0-3.2 引用溯源为半成品:后端响应已带 sources 字段(ai_routes.h 约 456 行),前端 AIChatPanel.vue 尚未展示来源,待补可点击来源。
- 详细方案看 P0-1_DESIGN.md(已实现);P0/P1/P2 全量规划看 MindVault-AI功能增强设计_20260905.md(已更新为已实现状态,含第十三章P0批次+第十四章P1-5+第十五章P1-6/P1-7已实现记录+API请求响应示例)。改 AI 功能时保持 /api/ai/chat 单轮调用兼容。新增端点 POST /api/ai/chat/stream(SSE流式)、POST /api/ai/action(内容创作统一接口)、GET /api/notes/:id/recommendations(关联推荐)。

## 硬性约定(踩过的坑)
1. 中文路径:SQLite 打开必须 sqlite3_open16(UTF-16 宽字符),ANSI 版打不开中文路径
2. 所有 SQL 一律参数化查询,禁止字符串拼 SQL(防注入)
3. FTS5 用独立虚拟表 + 触发器同步,不要用 content= 模式;改表结构需删旧 .db 重建
4. 响应格式走 utils::Success/Error,错误码与 message 语义别乱改(前端按它判断)
5. 前端 API 调用集中 api.ts;异步 try/catch + console.error;防抖用 setTimeout;不要引 lodash
6. Service 用 shared_ptr 持有,确保生命周期覆盖路由回调

## Git 工作流(每次会话都遵守)
- 分支 develop,远程 origin = github.com/lulu1011-pro/kelinLulu.git
- 完成一个可独立验证的小改动就 commit 一次,message 用中文,格式「模块:干了啥」
- commit 前先 git status 确认只含本次相关文件;不提交 build/、data/ 下的 .db、node_modules
- push 前先 git fetch + 检查是否落后远程,落后则 git pull --rebase 再 push,避免分叉
- 仓库根在 D:/kelin/AI项目(上级目录),别把根目录无关文件(mysql.cpp、txt 日志等)带进提交
