# MindVault 项目上下文文档

> 供 AI 编程助手（如 OpenCode）快速了解项目全貌，接续开发。

---

## 一、项目概述

**MindVault** 是一款本地优先的个人知识库/Wiki 桌面应用，核心理念是"用 Wiki 链接连接笔记"。

- **架构**：C++ 后端 REST API + Vue3 前端（前后端分离，浏览器访问）
- **定位**：类 Obsidian/Notion 的本地知识管理工具，但更轻量
- **开发模式**：AI 辅助开发，用户审查微调

---

## 二、技术栈

| 层级 | 技术 | 版本/说明 |
|------|------|-----------|
| 后端语言 | C++17 | MSVC (VS2022) |
| HTTP 框架 | Crow | header-only，类 Flask |
| 数据库 | SQLite3 + FTS5 | 编译 amalgamation，WAL 模式 |
| JSON | nlohmann/json | header-only |
| 构建 | CMake 3.20+ | 路径：`D:\VC2022\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` |
| 前端框架 | Vue 3 + Vite 8 | TypeScript |
| 编辑器 | CodeMirror 6 | markdown 语言支持 |
| Markdown 渲染 | marked.js 18 | 自定义 Wiki 链接扩展 |
| HTML→MD | turndown.js 7 | 剪贴板粘贴 + 文件导入 |

---

## 三、项目目录结构

```
mindvault-cpp/
├── CMakeLists.txt              # CMake 构建，MSVC UTF-8
├── PLAN.md                     # 原始重构方案（参考用，部分已过时）
├── CONTEXT.md                  # ← 本文件，项目上下文
├── RESUME.md                   # 简历项目描述
├── third_party/                # 头文件库（crow_all.h, sqlite3.c/h, nlohmann/json.hpp）
├── src/                        # C++ 后端源码
│   ├── main.cpp                # 入口：启动 Crow 服务器 (127.0.0.1:8080)
│   ├── database.h/cpp          # SQLite3 封装层（参数化查询、事务、UTF-16路径）
│   ├── models/
│   │   ├── note.h              # 笔记模型（FromJson/ToListJson/ToDetailJson）
│   │   └── tag.h               # 标签模型
│   ├── services/
│   │   ├── note_service.h      # 笔记 CRUD + Wiki 链接解析 + 回收站
│   │   ├── search_service.h    # FTS5 全文搜索 + LIKE 标题搜索
│   │   └── tag_service.h       # 标签 CRUD（自动创建 + 笔记关联）
│   ├── routes/
│   │   ├── note_routes.h       # /api/notes/* 全部路由
│   │   ├── search_routes.h     # /api/search 路由
│   │   └── file_routes.h       # /api/files/* + /api/tags 路由
│   └── utils/
│       ├── config.h            # 单例配置（db_path, host, port）
│       └── response.h          # 统一 JSON 响应 {ok, data/error}
├── web/                        # Vue3 前端
│   ├── vite.config.ts          # Vite 代理 /api → http://127.0.0.1:8080
│   ├── package.json            # 依赖：vue, codemirror, marked, turndown
│   └── src/
│       ├── main.ts
│       ├── App.vue             # 主布局：Sidebar + Editor + BacklinksPanel
│       ├── api.ts              # 封装所有后端 API（fetch + JSON）
│       ├── style.css           # Tokyo Night Premium 主题（30+ CSS 变量）
│       └── components/
│           ├── Sidebar.vue     # 侧边栏：搜索、文件夹树、笔记列表、回收站
│           ├── Editor.vue      # 编辑器：标题、标签栏、工具栏、CM6、预览、大纲
│           ├── FolderTree.vue  # 递归文件夹树组件
│           ├── BacklinksPanel.vue  # 反向/正向链接面板
│           ├── SearchModal.vue     # Ctrl+K 全局搜索弹框
│           └── OutlinePanel.vue    # Markdown 标题大纲导航
├── data/                       # 运行时数据（.db 文件）
└── build/                      # CMake 构建输出
```

---

## 四、数据库设计（5 张表）

```sql
-- 笔记表
notes (id, title, content, folder, is_deleted, sort_order, created_at, updated_at)

-- 标签表
tags (id, name UNIQUE)

-- 笔记-标签关联
note_tags (note_id, tag_id, PRIMARY KEY)

-- Wiki 链接有向图
link_edges (source_id, target_id, PRIMARY KEY)

-- FTS5 全文搜索虚拟表
notes_fts(title, content)  -- 独立存储，通过触发器同步
```

**FTS5 同步**：INSERT/UPDATE/DELETE 触发器自动同步 notes → notes_fts，搜索结果用 `>>>text<<<` 高亮标记。

---

## 五、API 一览（18 个端点）

### 笔记 CRUD
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/notes?folder=` | 笔记列表（支持 folder 过滤） |
| GET | `/api/notes/:id` | 笔记详情（含 content） |
| POST | `/api/notes` | 创建笔记 `{title, content, folder}` |
| PUT | `/api/notes/:id` | 更新笔记 `{title, content, folder?}` |
| DELETE | `/api/notes/:id` | 软删除（is_deleted=1） |

### 回收站
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/notes/trash` | 已删除笔记列表 |
| POST | `/api/notes/:id/restore` | 恢复笔记 |
| DELETE | `/api/notes/:id/permanent` | 永久删除 |
| DELETE | `/api/notes/trash/empty` | 清空回收站 |

### Wiki 链接
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/notes/:id/backlinks` | 谁链接到当前笔记 |
| GET | `/api/notes/:id/links` | 当前笔记链接到谁 |

### 搜索
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/search?q=` | FTS5 全文搜索 |
| GET | `/api/search/title?keyword=` | LIKE 标题模糊搜索 |

### 标签
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/tags` | 所有标签列表 |
| GET | `/api/notes/:id/tags` | 笔记的标签 |
| POST | `/api/notes/:id/tags` | 添加标签 `{name}` |
| DELETE | `/api/notes/:id/tags` | 移除标签 `{name}` |

### 文件
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/files/export/:id` | 导出为 .md |
| POST | `/api/files/import` | 导入文件 `{filename, content, folder}` |

### 标签（新增）
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/tags/:name/notes` | 按标签名获取笔记列表 |

### 排序（新增）
| 方法 | 路径 | 说明 |
|------|------|------|
| PUT | `/api/notes/:id/order` | 更新笔记排序 `{sort_order}` |
| PUT | `/api/notes/order` | 批量更新排序 `{orders: [{id, order}]}` |

### 图片
| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/upload/image` | multipart 上传图片 |
| POST | `/api/upload/base64` | base64 上传图片 |
| GET | `/uploads/:filename` | 访问图片 |

### 闪卡
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/notes/:id/flashcards` | 笔记的闪卡 |
| POST | `/api/flashcards` | 创建闪卡 `{note_id, front, back}` |
| PUT | `/api/flashcards/:id` | 更新闪卡 |
| DELETE | `/api/flashcards/:id` | 删除闪卡 |
| GET | `/api/flashcards/due` | 待复习闪卡 |
| POST | `/api/flashcards/:id/review` | 提交复习 `{quality}` |

### 分享
| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/share` | 创建分享链接 `{note_id, permission}` |
| GET | `/api/share/list` | 获取分享列表 |
| GET | `/api/share/:code` | 访问分享（无需登录） |
| DELETE | `/api/share/:id` | 删除分享 |

### 协作（新增）
| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/notes/:id/join` | 加入笔记编辑 |
| POST | `/api/notes/:id/leave` | 离开笔记编辑 |
| GET | `/api/notes/:id/viewers` | 查看在线用户 |
| POST | `/api/notes/:id/heartbeat` | 心跳（30秒超时） |

### 权限（新增）
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/notes/:id/permissions` | 获取笔记权限 |
| POST | `/api/notes/:id/permissions` | 添加权限 `{user_id, role}` |
| DELETE | `/api/notes/:id/permissions` | 删除权限 `{user_id}` |

### 其他
| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/` | 健康检查 |
| OPTIONS | `/api/*` | CORS 预检 |

**统一响应格式**：
```json
{"ok": true, "data": {...}}      // 成功
{"ok": false, "error": {"code": 400, "message": "..."}}  // 失败
```

---

## 六、前端组件关系

```
App.vue
├── Sidebar.vue
│   ├── FolderTree.vue (递归)
│   ├── 笔记列表（支持拖拽排序）
│   ├── 标签云区域（点击过滤笔记）
│   └── 回收站区域
├── Editor.vue
│   ├── 标题输入
│   ├── 标签栏 (tags-bar)
│   ├── Markdown 工具栏
│   ├── CodeMirror 6 编辑器
│   ├── Markdown 预览区（滚动同步大纲）
│   └── OutlinePanel.vue (大纲导航，支持滚动同步)
├── BacklinksPanel.vue
└── SearchModal.vue (Ctrl+K)
```

---

## 七、已完成功能清单 ✅

### 后端（C++）
- [x] SQLite3 封装层（参数化查询、UTF-16 路径、WAL 模式）
- [x] 笔记 CRUD + 软删除 + 回收站（恢复/永久删除/清空）
- [x] FTS5 全文搜索（独立虚拟表 + 触发器同步）
- [x] Wiki 链接解析 `[[标题]]` + 双向链接计算（有向图 link_edges）
- [x] 标签管理（自动创建、笔记关联、按 name 删除、按标签查笔记）
- [x] 笔记排序（sort_order 字段 + 批量更新排序）
- [x] 版本历史（versions 表 + 自动保存版本 + 版本列表/详情 API）
- [x] 图片上传（/api/upload/image + 静态文件服务 /uploads/*）
- [x] AI 路由（/api/ai/chat + RAG 检索）
- [x] 静态文件服务（/app/* 托管 web/dist）
- [x] 文件导入导出（.md 文件）
- [x] 统一 JSON 响应 + CORS
- [x] 数据库迁移（自动添加 sort_order 列）

### 前端（Vue3）
- [x] Tokyo Night Premium 暗色主题（30+ CSS 变量 + 过渡动画）
- [x] 暗/亮主题切换（SettingsModal + localStorage 持久化）
- [x] CodeMirror 6 Markdown 编辑器
  - 三种模式：编辑/预览/分栏
  - 行号、换行、one-dark 主题（Compartment 动态切换）
- [x] Markdown 实时预览（marked.js + 自定义 Wiki 链接扩展）
- [x] Wiki 链接 `[[标题]]` 渲染+点击跳转（三级查找：已加载→搜索→自动创建）
- [x] Markdown 工具栏（粗斜体/标题/列表/引用/代码/链接/表格 + 快捷键 Ctrl+B/I/K）
- [x] HTML → Markdown 自动转换（turndown.js：导入文件 + 剪贴板粘贴）
- [x] 图片上传（粘贴/拖拽图片自动上传 + 插入 Markdown）
- [x] PDF 导出（浏览器打印窗口，白色打印版式）
- [x] 文件夹树形结构（FolderTree.vue 递归组件，嵌套/折叠/智能图标）
- [x] 右键菜单（重命名、移动、删除确认、新建子文件夹）
- [x] 创建笔记弹框（输入标题 + 选文件夹）
- [x] 反向链接面板（BacklinksPanel.vue，反向/正向切换）
- [x] Ctrl+K 全局搜索弹框（SearchModal.vue，键盘导航）
- [x] 搜索高亮渲染（FTS5 `>>>text<<<` → `<mark>text</mark>`）
- [x] 回收站（恢复、永久删除、清空，二次确认）
- [x] 笔记大纲导航（OutlinePanel.vue，标题提取+点击跳转+滚动同步）
- [x] 标签栏（编辑器内显示标签、添加/删除标签）
- [x] 全局标签管理（Sidebar 标签云，点击过滤笔记）
- [x] 笔记拖拽排序（同文件夹内拖拽调整顺序）
- [x] 多标签页编辑（TabBar.vue，同时打开多个笔记）
- [x] 版本历史（VersionHistory.vue，查看历史版本+恢复）
- [x] AI 对话面板（AIChatPanel.vue，基于知识库的 RAG 问答）

---

## 八、待完成 / 可改进功能 🚧

### 低优先 / 进阶
- [ ] **Ollama 完整集成**：实际调用 Ollama API 生成回答（当前只有 RAG 检索框架）
- [ ] **数据加密**：AES-256 完整实现（需要 OpenSSL 库）
- [ ] **快捷键系统**：自定义快捷键面板
- [ ] **知识图谱可视化**：D3.js/Cytoscape 全局图谱

---

## 九、关键踩坑记录 ⚠️

1. **FTS5 `content=notes` 模式不可靠**：改用独立 FTS5 虚拟表 + 触发器同步。数据库结构变更时需删除旧 `.db` 文件重建。

2. **Windows 中文路径**：SQLite `sqlite3_open()` 用 ANSI 编码打不开中文路径，必须用 `sqlite3_open16()` + UTF-16 宽字符串。

3. **CodeMirror `lineWrapping`**：不是 `@codemirror/view` 的独立导出，需用 `EditorView.lineWrapping`。`Compartment` 用于动态切换扩展。

4. **marked.js 自定义语法**：`marked.renderer.renderer.link` 不存在，必须用 `marked.use({ extensions: [...] })` 注册。

5. **CORS 跨域**：前端开发时用 Vite 代理（`vite.config.ts` 的 `server.proxy`），`API_BASE` 改为相对路径 `/api`。

6. **编辑器防抖**：`EditorView.updateListener` 中不要同步做 `marked` 渲染，用 `setTimeout` 防抖。`watch` 监听 `noteContent` 会和编辑器保存形成循环，只监听 `noteId` 切换。

7. **Vue3 递归组件**：需单独文件（`FolderTree.vue`），不能用双 `<script>` 块混用 `defineComponent` 和 `<script setup>`。

8. **Wiki 链接导航**：点击 `[[标题]]` 需要三级查找（已加载笔记→搜索API→自动创建），只用搜索API会因为模糊匹配误判笔记不存在。

9. **删除标签 API**：后端 `RemoveTagFromNote` 接收 `tag_name`（非 `tag_id`），前端传 `{name: tagName}` 而非 tagId。DELETE 请求带 JSON body。

---

## 十、开发环境与运行

### 编译后端
```powershell
cd d:\kelin\AI项目\mindvault-cpp\build
& 'D:\VC2022\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build . --config Release
```

### 运行后端
```powershell
# 后端启动在 127.0.0.1:8080
.\build\Release\mindvault.exe
```

### 启动前端开发
```powershell
cd web
npm run dev    # Vite 开发服务器，自动代理 /api → localhost:8080
```

### 构建前端
```powershell
cd web
npm run build  # 输出到 web/dist/
```

---

## 十一、代码风格约定

### C++ 后端
- 命名空间 `mindvault`，子空间 `mindvault::services`, `mindvault::routes`, `mindvault::utils`, `mindvault::models`
- Service 层全在 `.h` 文件中（header-only，简化构建），方法内联
- 路由注册函数 `RegisterXxxRoutes(crow::App<>&, Database&)` 也在 `.h` 中
- 统一响应格式：`utils::Success(data)` / `utils::Error(msg)` / `utils::NotFound(resource)`
- Service 用 `shared_ptr` 持有，确保生命周期覆盖路由回调

### Vue3 前端
- `<script setup lang="ts">` 组合式 API
- CSS 用 CSS 变量（Tokyo Night 主题），不硬编码颜色
- API 调用封装在 `api.ts`，组件通过 `emit` 事件通知父组件
- 异步操作统一 `try/catch` + `console.error`
- 防抖用 `setTimeout`，不用 lodash

---

## 十二、架构设计决策

1. **前后端分离**：C++ 后端纯 API，Vue3 前端独立开发，Vite 代理解决 CORS
2. **软删除**：笔记删除先入回收站（`is_deleted=1`），支持恢复
3. **Wiki 链接有向图**：`link_edges` 表存储 source→target 关系，每次保存笔记时增量更新
4. **FTS5 独立表**：避免 `content=` 模式的坑，用触发器自动同步
5. **前端无状态管理库**：不用 Pinia，App.vue 集中管理状态，props/emit 传递
6. **CodeMirror Compartment**：动态切换换行/行号/主题，避免重建编辑器实例
