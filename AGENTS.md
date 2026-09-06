# MindVault 项目（仓库根引导）

> 本仓库（kelinLulu）的**代码主体在 `mindvault-cpp/` 子目录**。如果你被打开的目录是本仓库根，请先切换到 `mindvault-cpp/` 再读下面的项目全貌文件。

## 给 AI 的指引（每次会话必读）

1. 工作目录切到 `mindvault-cpp/`，先读 `mindvault-cpp/CLAUDE.md`（项目全貌：技术栈/目录/数据库表/API 约定/当前进度/硬性约定/Git 工作流）
2. 再读 `mindvault-cpp/CONTEXT.md` 与 `mindvault-cpp/P0-1_DESIGN.md` 了解背景（注意 CONTEXT.md 表清单已过时，以 CLAUDE.md 里的 database.cpp 实际表为准）
3. Git 仓库根在本目录（上级），当前开发分支 develop；只提交 mindvault-cpp/ 下的相关改动，别把仓库根散落文件（mysql.cpp、txt 日志等）带进提交
4. 动手改代码前，先向用户汇报：你理解的项目现状 + 本次要做什么 + 打算怎么改，确认后再动手
