# MindVault — 本地优先个人知识库

C++17（Crow + SQLite3/FTS5）后端 + Vue3 前端，AI 增强知识库。详细设计见 `MindVault-AI功能增强设计_20260905.md`，当前进度看 `CLAUDE.md`，AI 接手必读 `CLAUDE.md` + `CONTEXT.md`。

## 怎么打开 / 怎么跑（日常三件事）

### 1. 一键启动（最快，日常用这个）
双击根目录的 `start.bat`：
- 自动拉起后端 `build/Release/mindvault.exe`
- 自动拉起前端 `npm run dev`
- 浏览器自动打开 http://localhost:5173

想重启（改了代码重新编译后）：双击 `restart.bat`（先杀进程再拉起）。

### 2. 改后端代码（C++）用什么打开
用 **Visual Studio 2022**：
- 双击 `build/MindVault.sln` 直接打开整个工程
- 或者用根目录 `build.bat` 命令行编译（`cmake --build . --config Release`）
- 后端代码在 `src/`：main.cpp 入口、routes/ 各路由、services/ 业务逻辑、database.cpp 数据层
- 改完编译产物在 `build/Release/mindvault.exe`，配合 restart.bat 重启生效
- 注意：Crow、sqlite3、nlohmann/json 都在 `third_party/`，header-only，无外部包要装

### 3. 改前端代码（Vue3）用什么打开
用 **VS Code**（或任何前端 IDE）：
- 打开 `web/` 目录
- 终端里 `npm install`（第一次或依赖有变时）
- `npm run dev` 启动开发服务器（默认 5173，Vite 代理 /api → 8080）
- 前端代码在 `web/src/`：App.vue 主界面、components/ 各面板、api.ts 封装全部后端 API
- 改完浏览器热更新，不用重启

## 端口与地址
| 服务 | 地址 |
|------|------|
| 后端 API | http://127.0.0.1:8080 （健康检查 http://127.0.0.1:8080/api/ai/status） |
| 前端页面 | http://localhost:5173 |
| 数据库 | `data/mindvault.db`（每用户独立库 `user_<id>.db`，SQLite，WAL 模式） |

## 目录速览
- `src/` 后端：routes/（HTTP 路由）、services/（业务）、utils/（config、统一响应）
- `web/` 前端：Vue3 + Vite + TS
- `data/` SQLite 数据库（git 忽略）
- `build/` CMake 构建产物（git 忽略）
- 根目录 *.md：设计文档、PRD、架构、验收清单，AI 功能增强主设计在 `MindVault-AI功能增强设计_20260905.md`

## 首次运行前要配的
AI 功能需要 API Key：前端页面右上角 AI 面板 ⚙️ 里配（支持通义/豆包/OpenAI/自定义，配置存 localStorage）。纯笔记功能不需要 Key 也能用。
