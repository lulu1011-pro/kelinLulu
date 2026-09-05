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
AI 增强 P0-1「多轮会话记忆」开发中:ai_conversations/ai_messages 表已建,database.cpp 已迁移;ai_routes.h 已有会话级互斥锁、MAX_HISTORY_TURNS=10、MAX_CONTEXT_TOKENS=8000、system prompt 截断。详细方案看 P0-1_DESIGN.md;P0/P1/P2 全量规划看 MindVault-AI功能增强设计_20260905.md。改 AI 功能时保持 /api/ai/chat 单轮调用兼容。

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
