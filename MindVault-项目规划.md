# MindVault - 本地知识库桌面客户端

## 项目概览

**项目名称**: MindVault（心灵库）  
**项目定位**: 本地优先、隐私安全、AI增强的个人知识管理工具  
**核心愿景**: 打造"知识收集→整理→链接→检索→复用→创造"全流程闭环的个人第二大脑  
**目标用户**: 知识工作者、研究人员、学生、开发者，需要私有化知识管理的人群

---

## 一、产品功能分层规划

### 1.1 MVP最小可行核心（优先级 ★★★★★）

> **目标周期**: 4-6周  
> **策略**: 快速验证核心假设，建立产品底座

| 功能模块 | 具体功能 | 技术要点 |
|---------|---------|---------|
| 本地存储架构 | 全量数据本地存储、自定义存储路径、标准Markdown格式、多知识库隔离 | SQLite索引 + 文件系统 |
| Markdown编辑 | GFM语法、WYSIWYG+源码双模式、粘贴自动转Markdown、自动保存 | CodeMirror/Monaco Editor |
| 文件内容管理 | 多级文件夹、拖拽排序、基础标签、收藏夹、回收站 | 文件系统Watcher |
| 离线全文检索 | 毫秒级检索、关键词高亮、增量索引 | SQLite FTS5 / FlexSearch |
| 基础双向链接 | [[双链]]语法、点击跳转、反向链接面板 | Markdown解析 + 索引 |

### 1.2 核心特色功能（优先级 ★★★★☆）

> **目标周期**: 8-12周  
> **策略**: 打造知识库核心竞争力，区别于普通编辑器

| 功能模块 | 具体功能 | 技术要点 |
|---------|---------|---------|
| 进阶双向链接 | 块级引用、链接预览、别名重定向、未创建页面提示 | 自研解析引擎 |
| 知识图谱可视化 | 全局图谱、单页关联图谱、节点拖拽、筛选能力 | D3.js/Cytoscape |
| 结构化组织 | 多级嵌套标签、模板系统、闪卡/记忆卡片 | 间隔重复算法 |
| 附件管理 | 全格式附件、内容检索、统一管理面板、图片管理 | 附件解析库 |

### 1.3 AI增强功能（优先级 ★★★☆☆）

> **目标周期**: 12-16周  
> **策略**: 差异化竞争核心，主打隐私离线AI

| 功能模块 | 具体功能 | 技术要点 |
|---------|---------|---------|
| 本地RAG问答 | 向量检索、引用来源、离线LLM兼容 | Ollama/llama.cpp |
| AI内容创作 | 续写扩写、润色纠错、结构化处理 | LLM API集成 |
| AI知识梳理 | 自动摘要、智能标签、关联推荐、思维导图 | LLM + Graph |
| AI辅助工具 | 侧边栏对话、代码辅助、PDF解读 | LLM API |

### 1.4 进阶场景功能（优先级 ★★☆☆☆）

> **目标周期**: 16-24周+

| 功能模块 | 具体功能 | 技术要点 |
|---------|---------|---------|
| 导入导出 | 批量导入(Obsidian/Notion等)、多格式导出、静态站点生成 | 格式转换器 |
| 版本同步 | 本地版本历史、手动快照、WebDAV/网盘同步 | Diff算法 |
| 资料收集 | 网页剪藏、PDF批注、剪贴板收录 | 浏览器插件API |
| 效率工具 | 无限画布、待办任务、番茄钟 | Canvas/状态管理 |

### 1.5 桌面端专属（优先级 ★★★☆☆）

> **目标周期**: 8-12周（与核心功能并行）

| 功能模块 | 具体功能 | 技术要点 |
|---------|---------|---------|
| 全局快捷键 | 全局唤起、快速新建、全局检索 | Electron globalShortcut |
| 系统集成 | 右键菜单、系统托盘、多窗口分屏 | Electron API |
| 离线完全可用 | 100%离线功能、低资源占用 | 架构设计 |

---

## 二、技术架构设计

### 2.1 跨平台框架选型

| 方案 | 优势 | 劣势 | 推荐度 |
|-----|------|------|-------|
| **Electron** | 生态成熟、跨平台、Node.js生态丰富 | 体积较大、内存占用高 | ★★★★★ |
| Tauri | 体积小、性能好、Rust后端 | 生态较新、FFI复杂 | ★★★★☆ |
| Flutter | 渲染性能好、自绘UI | 桌面端成熟度一般 | ★★★☆☆ |
| NW.js | 简单易用 | 维护不活跃 | ★★☆☆☆ |

**推荐方案**: **Electron + Tauri混合**（核心用Tauri渲染，复杂功能用Electron）

> 或者：**纯Tauri方案**（Rust后端 + Web前端，长期维护成本低）

### 2.2 技术栈总览

```
┌─────────────────────────────────────────────────────────┐
│                     表现层 (Renderer)                     │
├─────────────────────────────────────────────────────────┤
│  Vue 3 / React 18  +  TypeScript  +  TailwindCSS        │
│  CodeMirror 6 / Monaco Editor (Markdown编辑)            │
│  D3.js / Cytoscape.js (知识图谱)                        │
│  Naive UI / shadcn/ui (组件库)                          │
└─────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────┐
│                      逻辑层 (Main)                        │
├─────────────────────────────────────────────────────────┤
│  Node.js / Bun  (Electron) 或  Rust (Tauri)             │
│  SQLite (FTS5全文检索 + 结构化数据)                      │
│  File System API (Markdown文件读写)                      │
│  Chokidar (文件监控、增量索引)                           │
└─────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────┐
│                      AI层 (可选)                         │
├─────────────────────────────────────────────────────────┤
│  Ollama (本地LLM) / OpenAI API / 通义千问API            │
│  Chroma / Qdrant (向量数据库，本地可选)                  │
│  LangChain / LlamaIndex (RAG框架)                       │
└─────────────────────────────────────────────────────────┘
```

### 2.3 核心模块架构

```
mindvault/
├── src/
│   ├── main/                    # 主进程 (Electron/Tauri)
│   │   ├── index.ts            # 入口
│   │   ├── ipc/                # IPC通信处理
│   │   ├── services/
│   │   │   ├── file.service.ts # 文件系统操作
│   │   │   ├── db.service.ts   # SQLite操作
│   │   │   ├── search.service.ts # 全文检索
│   │   │   └── sync.service.ts # 同步服务
│   │   └── utils/
│   │       ├── parser.ts       # Markdown解析
│   │       └── linker.ts       # 双向链接处理
│   │
│   ├── renderer/               # 渲染进程 (Vue3)
│   │   ├── views/
│   │   │   ├── workspace.vue   # 主工作区
│   │   │   ├── editor.vue      # Markdown编辑器
│   │   │   ├── graph.vue       # 知识图谱
│   │   │   └── settings.vue    # 设置面板
│   │   ├── components/
│   │   │   ├── sidebar/        # 侧边栏组件
│   │   │   ├── editor/         # 编辑器组件
│   │   │   └── graph/          # 图谱组件
│   │   ├── stores/            # Pinia状态管理
│   │   └── composables/       # 组合式函数
│   │
│   └── preload/               # 预加载脚本 (安全桥接)
│
├── resources/                 # 静态资源
│   ├── icons/
│   ├── themes/
│   └── templates/             # 内置模板
│
├── tests/                     # 测试
│   ├── unit/
│   └── e2e/
│
└── scripts/                   # 构建脚本
```

### 2.4 数据模型设计

```sql
-- 知识库元数据
CREATE TABLE vaults (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    path TEXT NOT NULL UNIQUE,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 笔记索引
CREATE TABLE notes (
    id TEXT PRIMARY KEY,
    vault_id TEXT NOT NULL,
    file_path TEXT NOT NULL,
    title TEXT NOT NULL,
    tags TEXT,  -- JSON数组
    created_at DATETIME,
    modified_at DATETIME,
    FOREIGN KEY (vault_id) REFERENCES vaults(id)
);

-- 双向链接关系
CREATE TABLE links (
    id TEXT PRIMARY KEY,
    source_id TEXT NOT NULL,
    target_id TEXT,
    target_title TEXT,  -- 未创建页面
    link_type TEXT,    -- 'page' | 'block' | 'heading'
    created_at DATETIME
);

-- 标签体系
CREATE TABLE tags (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    parent_id TEXT,    -- 父标签（支持嵌套）
    color TEXT,
    vault_id TEXT NOT NULL
);

-- 全文检索（SQLite FTS5）
CREATE VIRTUAL TABLE notes_fts USING fts5(
    title,
    content,
    tags,
    content='notes',
    content_rowid='rowid'
);

-- 版本历史
CREATE TABLE versions (
    id TEXT PRIMARY KEY,
    note_id TEXT NOT NULL,
    content TEXT NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);
```

### 2.5 AI集成架构

```
                    ┌─────────────────┐
                    │   用户隐私优先   │
                    └────────┬────────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
        ┌─────────┐   ┌──────────┐   ┌──────────┐
        │ 本地LLM  │   │ 在线API  │   │ 混合模式 │
        │(Ollama) │   │(OpenAI)  │   │          │
        └─────────┘   └──────────┘   └──────────┘
              │              │              │
              └──────────────┼──────────────┘
                             ▼
                    ┌─────────────────┐
                    │   RAG Pipeline  │
                    ├─────────────────┤
                    │ 1. Query理解     │
                    │ 2. 检索增强      │
                    │ 3. 生成回答      │
                    │ 4. 引用溯源      │
                    └─────────────────┘
```

---

## 三、开发路线图

### Phase 1: MVP核心底座（4-6周）

| 周次 | 任务 | 交付物 |
|-----|------|-------|
| Week 1 | 项目初始化、Electron/Tauri搭建、基础UI框架 | 可运行空壳应用 |
| Week 2 | 本地存储架构、多知识库管理 | 知识库创建/切换功能 |
| Week 3 | Markdown编辑器开发（双模式） | 编辑器核心功能 |
| Week 4 | 文件管理（文件夹、标签、搜索） | 文件树+标签+检索 |
| Week 5 | 基础双向链接 | [[双链]]语法+跳转 |
| Week 6 | 优化打磨、测试修复 | MVP Release |

**MVP交付标准**:
- [ ] 可创建多个知识库
- [ ] Markdown编辑双模式正常
- [ ] 全文检索毫秒响应
- [ ] 基础双向链接可用
- [ ] 100%离线可用

### Phase 2: 核心特色（8-12周）

| 阶段 | 任务 | 周期 |
|-----|------|------|
| 2.1 | 进阶双向链接（块级引用、链接预览） | 3周 |
| 2.2 | 知识图谱可视化 | 3周 |
| 2.3 | 模板系统 + 闪卡系统 | 3周 |
| 2.4 | 附件管理 + PDF阅读器 | 3周 |

### Phase 3: AI增强（12-16周）

| 阶段 | 任务 | 周期 |
|-----|------|------|
| 3.1 | 本地RAG架构 + Ollama集成 | 4周 |
| 3.2 | AI对话 + 内容创作 | 4周 |
| 3.3 | AI知识梳理 + 辅助工具 | 4周 |
| 3.4 | 多API兼容 + 提示词模板 | 2周 |

### Phase 4: 进阶功能（16-24周+）

- 导入导出（Obsidian/Notion等）
- 版本管理与同步
- 资料收集（网页剪藏、PDF批注）
- 效率工具（画布、待办、番茄钟）

### Phase 5: 桌面端专属 + 生态（持续迭代）

- 全局快捷键 + 系统集成
- 插件系统
- 主题定制
- 多平台适配

---

## 四、关键决策点

### 4.1 框架选择建议

**推荐: Tauri 2.0 + Vue 3**

理由:
1. **体积小**: 安装包 < 10MB vs Electron 150MB+
2. **性能好**: Rust原生性能，内存占用低
3. **安全性**: 默认沙箱，IPC更安全
4. **维护成本**: Rust后端长期维护压力小
5. **AI集成**: 便于后续集成本地LLM推理

### 4.2 编辑器选型

| 方案 | 适用场景 | 推荐度 |
|-----|---------|-------|
| CodeMirror 6 | 轻量级、需要深度定制 | ★★★★☆ |
| Monaco Editor | 接近VS Code体验、占用较高 | ★★★★★ |
| ProseMirror/TipTap | 协作、富文本编辑 | ★★★☆☆ |
| Milkdown | 专注文档、插件化 | ★★★★☆ |

**推荐: CodeMirror 6**（轻量可定制）或 **Monaco**（追求体验）

### 4.3 知识图谱选型

| 方案 | 特点 | 推荐度 |
|-----|------|-------|
| D3.js Force Graph | 灵活、定制性强、学习曲线陡 | ★★★★☆ |
| Cytoscape.js | 图论算法丰富、性能好 | ★★★★☆ |
| Sigma.js | WebGL渲染、大规模图 | ★★★☆☆ |
|vis.js | 简单易用、性能一般 | ★★★☆☆ |

**推荐: D3.js**（灵活性优先）或 **Cytoscape.js**（性能优先）

---

## 五、竞品分析

| 产品 | 优势 | 劣势 | MindVault差异化 |
|-----|------|------|----------------|
| Obsidian | 社区活跃、插件生态完善 | 无官方移动端、AI需付费 | 更强AI集成、更好隐私保护 |
| Notion | 协作能力强、UI精美 | 数据在云端、价格贵 | 纯本地、离线优先 |
| 思源笔记 | 块级编辑、所见即所得 | 同步付费、社区较小 | 更开放架构、AI原生设计 |
| Logseq | 大纲笔记、开源免费 | 功能相对简单 | 更强编辑+图谱+AI |

---

## 六、风险与应对

| 风险 | 影响 | 应对策略 |
|-----|------|---------|
| Electron体积大 | 用户体验差 | 评估迁移Tauri，或使用electron-builder优化 |
| 全文检索性能 | 大库体验差 | SQLite FTS5优化 + 增量索引 |
| AI集成复杂性 | 开发周期长 | 分层解耦，先实现API模式再本地 |
| 跨平台兼容性 | 测试成本高 | 使用Playwright进行E2E测试 |

---

## 七、成功指标

### MVP成功标准
- [ ] 核心功能稳定，无崩溃
- [ ] 1000+笔记规模下检索 < 100ms
- [ ] 基础双向链接断链率 < 1%
- [ ] 用户完成核心任务路径 < 3步

### 产品成功标准
- [ ] 核心用户留存率 > 60%
- [ ] 知识图谱使用率 > 40%
- [ ] AI功能使用率 > 30%
- [ ] NPS > 40

---

*文档版本: v1.0*  
*创建日期: 2026-04-29*  
*负责人: 高级项目经理*
