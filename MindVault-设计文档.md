# MindVault 技术设计文档

## 文档信息

| 字段 | 内容 |
|-----|------|
| 项目名称 | MindVault 本地知识库 |
| 文档版本 | v1.0 |
| 创建日期 | 2026-04-29 |
| 状态 | MVP技术设计 |

---

## 一、技术选型

### 1.1 核心决策

| 层级 | 推荐方案 | 原因 |
|-----|---------|------|
| **跨平台框架** | **Tauri 2.0** | 体积小(10MB vs 150MB+)、Rust性能、内存占用低 |
| **前端框架** | **Vue 3 + TypeScript** | Composition API、类型安全、生态成熟 |
| **UI组件库** | **Naive UI** | Vue3原生、主题定制方便 |
| **Markdown编辑器** | **CodeMirror 6** | 轻量、可深度定制、插件丰富 |
| **数据库** | **better-sqlite3 (SQLite)** | FTS5全文检索、本地无需服务 |
| **图谱可视化** | **D3.js** | 灵活定制、社区活跃 |

### 1.2 技术栈总览

```
前端: Vue 3 + TypeScript + Vite + Pinia + TailwindCSS
编辑器: CodeMirror 6 + Markdown插件集
数据库: SQLite (better-sqlite3) + FTS5
图谱: D3.js Force Layout
构建: Tauri CLI + electron-builder (备用)
测试: Vitest + Playwright
```

---

## 二、系统架构

### 2.1 整体架构图

```
┌─────────────────────────────────────────────────────────┐
│                   渲染进程 (Vue 3)                        │
│  ┌───────────┐  ┌───────────┐  ┌───────────┐        │
│  │  Workspace│  │   Editor  │  │   Graph   │        │
│  └─────┬─────┘  └─────┬─────┘  └─────┬─────┘        │
│        └──────────────┬────────────────┘               │
│                        ▼                                │
│                  Pinia Stores                          │
└────────────────────────┼────────────────────────────────┘
                         │ IPC (contextBridge)
┌────────────────────────┼────────────────────────────────┐
│                   主进程 (Tauri/Rust)                     │
│    ┌──────────┐  ┌──────────┐  ┌──────────┐           │
│    │  File    │  │    DB    │  │  Search  │           │
│    │ Service  │  │  Service │  │  Service │           │
│    └────┬─────┘  └────┬─────┘  └────┬─────┘           │
│         │              │              │                   │
│         ▼              ▼              ▼                   │
│    ┌──────────────────────────────────────┐             │
│    │         SQLite Database              │             │
│    │  (notes / links / tags / FTS5)      │             │
│    └──────────────────────────────────────┘             │
└─────────────────────────────────────────────────────────┘
```

### 2.2 数据流向

```
用户编辑 → CodeMirror内容变更 → 防抖保存(500ms)
    ↓
写入 .md 文件（文件系统）
    ↓
更新 SQLite 元数据（标题、标签、时间）
    ↓
更新 FTS5 全文索引（增量）
    ↓
通知其他组件刷新（Event Bus）
```

---

## 三、数据库设计

### 3.1 核心表结构

```sql
-- 知识库表
CREATE TABLE vaults (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    path TEXT NOT NULL UNIQUE,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
);

-- 笔记索引表
CREATE TABLE notes (
    id TEXT PRIMARY KEY,
    vault_id TEXT NOT NULL,
    file_path TEXT NOT NULL,
    title TEXT NOT NULL,
    tags TEXT DEFAULT '[]',  -- JSON数组
    created_at DATETIME,
    modified_at DATETIME,
    is_deleted INTEGER DEFAULT 0,
    FOREIGN KEY (vault_id) REFERENCES vaults(id)
);
CREATE INDEX idx_notes_vault ON notes(vault_id);
CREATE INDEX idx_notes_modified ON notes(modified_at DESC);

-- 双向链接表
CREATE TABLE links (
    id TEXT PRIMARY KEY,
    source_id TEXT NOT NULL,
    target_id TEXT,           -- NULL表示未创建
    target_title TEXT NOT NULL,
    link_type TEXT DEFAULT 'page',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (source_id) REFERENCES notes(id) ON DELETE CASCADE
);
CREATE INDEX idx_links_source ON links(source_id);
CREATE INDEX idx_links_target ON links(target_title);

-- 标签表（无限层级）
CREATE TABLE tags (
    id TEXT PRIMARY KEY,
    vault_id TEXT NOT NULL,
    name TEXT NOT NULL,
    parent_id TEXT,  -- 父标签
    color TEXT,
    FOREIGN KEY (vault_id) REFERENCES vaults(id) ON DELETE CASCADE
);

-- 全文检索（FTS5）
CREATE VIRTUAL TABLE notes_fts USING fts5(
    note_id UNINDEXED,
    title,
    content,
    tags,
    content='notes',
    content_rowid='rowid'
);
```

### 3.2 文件系统结构

```
知识库根目录/
├── .mindvault/           # 应用数据（隐藏）
│   ├── mindvault.db     # SQLite数据库
│   └── config.json     # 本地配置
├── 文件夹1/
│   ├── 笔记1.md
│   └── 笔记2.md
├── 文件夹2/
│   └── 子文件夹/
│       └── 笔记3.md
└── attachments/         # 附件目录
    └── 图片.png
```

---

## 四、核心模块设计

### 4.1 文件服务 (FileService)

```typescript
// src/main/services/file.service.ts
export class FileService {
    // 创建笔记
    async createNote(vaultPath: string, title: string): Promise<string> {
        const fileName = this.sanitizeFileName(title) + '.md';
        const filePath = path.join(vaultPath, fileName);
        await fs.writeFile(filePath, `# ${title}\n\n`, 'utf-8');
        return filePath;
    }

    // 读取笔记
    async readNote(filePath: string): Promise<string> {
        return await fs.readFile(filePath, 'utf-8');
    }

    // 保存笔记（防抖由渲染进程控制）
    async writeNote(filePath: string, content: string): Promise<void> {
        await fs.writeFile(filePath, content, 'utf-8');
    }

    // 监听文件变化（增量索引）
    watchFolder(vaultPath: string): FSWatcher {
        return chokidar.watch(path.join(vaultPath, '**/*.md'), {
            ignored: /(^|[\/\\])\./,  // 忽略隐藏文件
            persistent: true
        });
    }

    // 文件名安全处理
    private sanitizeFileName(name: string): string {
        return name.replace(/[<>:"/\\|?*]/g, '_').trim();
    }
}
```

### 4.2 数据库服务 (DBService)

```typescript
// src/main/services/db.service.ts
import Database from 'better-sqlite3';

export class DBService {
    private db: Database.Database;

    constructor(dbPath: string) {
        this.db = new Database(dbPath);
        this.initSchema();
    }

    // 初始化数据库结构
    private initSchema() {
        const schema = fs.readFileSync(
            path.join(__dirname, '../schema.sql'), 'utf-8'
        );
        this.db.exec(schema);
    }

    // 创建笔记索引
    createNote(data: {
        id: string, vaultId: string, filePath: string,
        title: string, tags?: string[]
    }) {
        const stmt = this.db.prepare(`
            INSERT INTO notes (id, vault_id, file_path, title, tags)
            VALUES (?, ?, ?, ?, ?)
        `);
        stmt.run(
            data.id, data.vaultId, data.filePath, data.title,
            JSON.stringify(data.tags || [])
        );

        // 更新FTS索引
        this.db.prepare(`
            INSERT INTO notes_fts (note_id, title, content, tags)
            VALUES (?, ?, '', ?)
        `).run(data.id, data.title, JSON.stringify(data.tags || []));
    }

    // 全文检索
    search(query: string, vaultId: string) {
        const stmt = this.db.prepare(`
            SELECT n.* FROM notes n
            JOIN notes_fts f ON n.id = f.note_id
            WHERE notes_fts MATCH ? AND n.vault_id = ?
            ORDER BY rank
            LIMIT 50
        `);
        return stmt.all(query, vaultId);
    }

    // 获取反向链接
    getBacklinks(targetTitle: string, vaultId: string) {
        const stmt = this.db.prepare(`
            SELECT l.*, n.title as source_title
            FROM links l
            JOIN notes n ON l.source_id = n.id
            WHERE l.target_title = ? AND n.vault_id = ?
        `);
        return stmt.all(targetTitle, vaultId);
    }
}
```

### 4.3 搜索服务 (SearchService)

```typescript
// src/main/services/search.service.ts
export class SearchService {
    constructor(private dbService: DBService) {}

    // 全文检索（FTS5）
    async search(
        query: string,
        vaultId: string,
        options?: { limit?: number; offset?: number }
    ): Promise<SearchResult[]> {
        // 构建FTS查询（处理特殊字符）
        const ftsQuery = this.buildFTSQuery(query);

        return this.dbService.search(ftsQuery, vaultId);
    }

    // 构建FTS5查询语法
    private buildFTSQuery(query: string): string {
        // 简单实现：拆分关键词，用AND连接
        const terms = query.split(/\s+/).filter(t => t.length > 0);
        return terms.map(t => `"${t}"*`).join(' AND ');
    }

    // 重建索引（全量）
    async rebuildIndex(vaultPath: string, vaultId: string) {
        // 1. 清空现有索引
        // 2. 遍历所有.md文件
        // 3. 提取标题、内容、标签
        // 4. 批量插入FTS
    }
}
```

---

## 五、组件设计

### 5.1 Vue组件架构

```
src/renderer/
├── App.vue                    # 根组件
├── layouts/
│   └── MainLayout.vue        # 主布局（左侧边栏+右工作区）
├── views/
│   ├── WorkspaceView.vue     # 工作区视图
│   ├── EditorView.vue        # 编辑器视图
│   ├── GraphView.vue         # 知识图谱视图
│   └── SettingsView.vue      # 设置视图
├── components/
│   ├── sidebar/
│   │   ├── SidebarContainer.vue
│   │   ├── FileTree.vue     # 文件树
│   │   ├── TagPanel.vue     # 标签面板
│   │   └── SearchBox.vue    # 搜索框
│   ├── editor/
│   │   ├── NoteEditor.vue   # 编辑器容器
│   │   ├── MarkdownPreview.vue  # WYSIWYG渲染
│   │   ├── SourceEditor.vue # 源码编辑
│   │   ├── Toolbar.vue      # 工具栏
│   │   └── BacklinksPanel.vue  # 反向链接
│   └── graph/
│       └── KnowledgeGraph.vue  # 知识图谱
└── stores/
    ├── vault.store.ts        # 知识库状态
    ├── editor.store.ts       # 编辑器状态
    ├── search.store.ts       # 搜索状态
    └── graph.store.ts        # 图谱状态
```

### 5.2 关键组件接口

```typescript
// NoteEditor.vue
interface EditorProps {
    noteId: string;
    mode: 'wysiwyg' | 'source';
    readonly?: boolean;
}

interface EditorEmits {
    (e: 'save', content: string): void;
    (e: 'link-click', noteId: string): void;
    (e: 'mode-change', mode: string): void;
}

// FileTree.vue
interface FileTreeNode {
    id: string;
    name: string;
    type: 'file' | 'folder';
    children?: FileTreeNode[];
    path: string;
    noteId?: string;
}

// KnowledgeGraph.vue
interface GraphNode {
    id: string;
    label: string;
    size: number;  // 引用频次
    group?: string; // 分组（按文件夹/标签）
}

interface GraphEdge {
    source: string;
    target: string;
    weight: number;
}
```

---

## 六、IPC通信协议

### 6.1 预加载脚本 (contextBridge)

```typescript
// src/preload/index.ts
import { contextBridge, ipcRenderer } from 'electron';

const api = {
    // 文件操作
    file: {
        createNote: (vaultId: string, title: string) =>
            ipcRenderer.invoke('file:create-note', vaultId, title),
        readNote: (noteId: string) =>
            ipcRenderer.invoke('file:read-note', noteId),
        writeNote: (noteId: string, content: string) =>
            ipcRenderer.invoke('file:write-note', noteId, content),
        deleteNote: (noteId: string) =>
            ipcRenderer.invoke('file:delete-note', noteId),
    },

    // 数据库操作
    db: {
        search: (query: string, vaultId: string) =>
            ipcRenderer.invoke('db:search', query, vaultId),
        getBacklinks: (noteId: string) =>
            ipcRenderer.invoke('db:get-backlinks', noteId),
        createTag: (vaultId: string, name: string, parentId?: string) =>
            ipcRenderer.invoke('db:create-tag', vaultId, name, parentId),
    },

    // 事件监听
    on: (channel: string, callback: Function) => {
        ipcRenderer.on(channel, (_, ...args) => callback(...args));
    }
};

contextBridge.exposeInMainWorld('api', api);
```

---

## 七、性能优化策略

### 7.1 编辑器优化

- **防抖保存**: 500ms间隔，避免频繁IO
- **虚拟滚动**: 大文档分块渲染
- **Web Worker**: Markdown解析放到Worker线程

### 7.2 搜索优化

- **FTS5增量索引**: 只更新变更笔记
- **结果缓存**: 热门查询缓存结果
- **分页加载**: 一次最多返回50条结果

### 7.3 内存优化

- **懒加载**: 文件树展开时才加载子节点
- **对象池**: 图谱节点复用
- **图片压缩**: 附件预览自动压缩

---

## 八、安全设计

### 8.1 数据安全

| 安全措施 | 实现方式 |
|---------|---------|
| 本地存储 | 数据100%在用户磁盘，无强制上传 |
| 加密存储 | AES-256加密整个知识库（可选） |
| 单篇加密 | 敏感笔记单独密码保护 |
| 自动备份 | 定时备份到本地/外部存储 |

### 8.2 应用安全

| 安全措施 | 实现方式 |
|---------|---------|
| IPC隔离 | contextBridge预加载，只暴露必要API |
| CSP策略 | Content-Security-Policy限制资源加载 |
| 输入验证 | 所有用户输入白名单验证 |
| 路径安全 | 防止路径遍历攻击 |

---

*文档版本: v1.0*  
*最后更新: 2026-04-29*  
*负责人: 高级项目经理*
