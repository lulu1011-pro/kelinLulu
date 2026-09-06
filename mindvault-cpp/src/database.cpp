// MindVault SQLite3 Database Layer - Implementation

#include "database.h"
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace mindvault {

Database::Database(const std::string& db_path) {
    // On Windows, sqlite3_open uses ANSI encoding which fails with Chinese paths.
    // Use sqlite3_open16 with UTF-16 wide string instead.
    int rc;
#ifdef _WIN32
    // Convert UTF-8 path to UTF-16 wide string
    int wlen = MultiByteToWideChar(CP_UTF8, 0, db_path.c_str(), -1, nullptr, 0);
    std::wstring wpath(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, db_path.c_str(), -1, &wpath[0], wlen);
    rc = sqlite3_open16(wpath.c_str(), &db_);
#else
    rc = sqlite3_open(db_path.c_str(), &db_);
#endif
    if (rc != SQLITE_OK) {
        std::string err = "Cannot open database: " + std::string(sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error(err);
    }
    // Enable WAL mode for better concurrent read/write performance
    Execute("PRAGMA journal_mode=WAL");
    Execute("PRAGMA foreign_keys=ON");
    std::cout << "[DB] Opened: " << db_path << std::endl;
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
        std::cout << "[DB] Closed." << std::endl;
    }
}

Database::Database(const std::filesystem::path& db_path) {
    // 直接用宽字符路径打开，避免中文编码问题
    int rc;
#ifdef _WIN32
    rc = sqlite3_open16(db_path.wstring().c_str(), &db_);
#else
    rc = sqlite3_open(db_path.string().c_str(), &db_);
#endif
    if (rc != SQLITE_OK) {
        std::string err = "Cannot open database: " + std::string(sqlite3_errmsg(db_));
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error(err);
    }
    Execute("PRAGMA journal_mode=WAL");
    Execute("PRAGMA foreign_keys=ON");
    // 输出实际路径用于调试
    std::cout << "[DB] Opened: " << db_path.u8string() << std::endl;
}

void Database::Init() {
    // ─── 用户表 ───
    Execute(R"(
        CREATE TABLE IF NOT EXISTS users (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            username    TEXT NOT NULL UNIQUE,
            password    TEXT NOT NULL,
            nickname    TEXT DEFAULT '',
            avatar      TEXT DEFAULT '',
            created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime')),
            updated_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
        )
    )");

    // ─── 笔记表 ───
    Execute(R"(
        CREATE TABLE IF NOT EXISTS notes (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id     INTEGER DEFAULT 0,
            title       TEXT NOT NULL DEFAULT '',
            content     TEXT NOT NULL DEFAULT '',
            folder      TEXT NOT NULL DEFAULT 'default',
            is_deleted  INTEGER NOT NULL DEFAULT 0,
            sort_order  INTEGER NOT NULL DEFAULT 0,
            created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime')),
            updated_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
        )
    )");

    // 迁移：添加 sort_order 列（如果不存在）
    Execute(R"(
        CREATE TABLE IF NOT EXISTS _migration_check (col TEXT)
    )");
    auto cols = Query("PRAGMA table_info(notes)");
    bool has_sort_order = false;
    for (auto& col : cols) {
        if (col["name"].get<std::string>() == "sort_order") {
            has_sort_order = true;
            break;
        }
    }
    if (!has_sort_order) {
        Execute("ALTER TABLE notes ADD COLUMN sort_order INTEGER NOT NULL DEFAULT 0");
        // 用现有顺序初始化
        Execute("UPDATE notes SET sort_order = id");
    }
    Execute("DROP TABLE IF EXISTS _migration_check");

    // 迁移：添加 user_id 列（如果不存在）
    bool has_user_id = false;
    auto cols2 = Query("PRAGMA table_info(notes)");
    for (auto& col : cols2) {
        if (col["name"].get<std::string>() == "user_id") {
            has_user_id = true;
            break;
        }
    }
    if (!has_user_id) {
        Execute("ALTER TABLE notes ADD COLUMN user_id INTEGER DEFAULT 0");
    }

    // ─── 标签表 ───
    Execute(R"(
        CREATE TABLE IF NOT EXISTS tags (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE
        )
    )");

    // ─── 笔记-标签关联表 ───
    Execute(R"(
        CREATE TABLE IF NOT EXISTS note_tags (
            note_id INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
            tag_id  INTEGER NOT NULL REFERENCES tags(id)  ON DELETE CASCADE,
            PRIMARY KEY (note_id, tag_id)
        )
    )");

    // ─── Wiki 链接边表（有向图） ───
    Execute(R"(
        CREATE TABLE IF NOT EXISTS link_edges (
            source_id INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
            target_id INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
            PRIMARY KEY (source_id, target_id)
        )
    )");
    Execute("CREATE INDEX IF NOT EXISTS idx_link_source ON link_edges(source_id)");
    Execute("CREATE INDEX IF NOT EXISTS idx_link_target ON link_edges(target_id)");

    // ─── 版本历史表 ───
    Execute(R"(
        CREATE TABLE IF NOT EXISTS versions (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            note_id     INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
            title       TEXT NOT NULL,
            content     TEXT NOT NULL,
            created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime'))
        )
    )");
    Execute("CREATE INDEX IF NOT EXISTS idx_versions_note ON versions(note_id)");

    // ─── 闪卡表 ───
    Execute(R"(
        CREATE TABLE IF NOT EXISTS flashcards (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            note_id       INTEGER NOT NULL REFERENCES notes(id) ON DELETE CASCADE,
            front         TEXT NOT NULL,
            back          TEXT NOT NULL,
            ease_factor   REAL DEFAULT 2.5,
            interval_days INTEGER DEFAULT 0,
            next_review   TEXT,
            created_at    TEXT NOT NULL DEFAULT (datetime('now','localtime'))
        )
    )");
    Execute("CREATE INDEX IF NOT EXISTS idx_flashcards_review ON flashcards(next_review)");

    // ─── 权限表 ───
    Execute(R"(
        CREATE TABLE IF NOT EXISTS permissions (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            note_id     INTEGER NOT NULL,
            user_id     INTEGER NOT NULL,
            role        TEXT NOT NULL DEFAULT 'viewer',
            created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime')),
            UNIQUE(note_id, user_id)
        )
    )");
    Execute("CREATE INDEX IF NOT EXISTS idx_permissions_note ON permissions(note_id)");
    Execute("CREATE INDEX IF NOT EXISTS idx_permissions_user ON permissions(user_id)");

// ─── FTS5 全文搜索虚拟表 ───
    Execute(R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING fts5(title, content)
    )");

    // ─── FTS5 同步 ───
    // 注：不再使用触发器，改由 NoteService 层手动同步，更可靠
    // 旧触发器如存在则删除
    Execute("DROP TRIGGER IF EXISTS notes_ai");
    Execute("DROP TRIGGER IF EXISTS notes_au");
    Execute("DROP TRIGGER IF EXISTS notes_ad");

    // ─── AI 会话表 ───
    Execute(R"(
        CREATE TABLE IF NOT EXISTS ai_conversations (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id     INTEGER NOT NULL,
            title       TEXT NOT NULL DEFAULT '新对话',
            is_deleted  INTEGER NOT NULL DEFAULT 0,
            deleted_at  TEXT DEFAULT NULL,
            created_at  TEXT NOT NULL DEFAULT (datetime('now','localtime')),
            updated_at  TEXT NOT NULL DEFAULT (datetime('now','localtime')),
            FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
        )
    )");
    Execute("CREATE INDEX IF NOT EXISTS idx_ai_conversations_user ON ai_conversations(user_id, updated_at DESC) WHERE is_deleted = 0");

    // ─── AI 消息表 ───
    Execute(R"(
        CREATE TABLE IF NOT EXISTS ai_messages (
            id               INTEGER PRIMARY KEY AUTOINCREMENT,
            conversation_id  INTEGER NOT NULL,
            role             TEXT NOT NULL CHECK (role IN ('user','assistant')),
            content          TEXT NOT NULL,
            tokens           INTEGER DEFAULT 0,
            created_at       TEXT NOT NULL DEFAULT (datetime('now','localtime')),
            FOREIGN KEY (conversation_id) REFERENCES ai_conversations(id) ON DELETE CASCADE
        )
    )");
    Execute("CREATE INDEX IF NOT EXISTS idx_ai_messages_conv ON ai_messages(conversation_id, created_at)");

    std::cout << "[DB] Schema initialized (notes, tags, note_tags, link_edges, notes_fts, ai_conversations, ai_messages)" << std::endl;
}

// ─── 基础执行 ───

void Database::Execute(const std::string& sql) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string err = "SQL error: " + std::string(err_msg);
        sqlite3_free(err_msg);
        throw std::runtime_error(err);
    }
}

void Database::Execute(const std::string& sql, const std::vector<nlohmann::json>& params) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        std::cerr << "[DB] Prepare failed for SQL: " << sql << std::endl;
        std::cerr << "[DB] Error: " << err << std::endl;
        throw std::runtime_error("Prepare failed: " + err);
    }
    BindParams(stmt, params);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(db_);
        std::cerr << "[DB] Execute failed for SQL: " << sql << std::endl;
        std::cerr << "[DB] Error: " << err << std::endl;
        sqlite3_finalize(stmt);
        throw std::runtime_error("Execute failed: " + err);
    }
    sqlite3_finalize(stmt);
}

// ─── 查询 ───

nlohmann::json Database::Query(const std::string& sql, const std::vector<nlohmann::json>& params) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Prepare failed: " + std::string(sqlite3_errmsg(db_)));
    }
    BindParams(stmt, params);

    nlohmann::json rows = nlohmann::json::array();
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        rows.push_back(RowToJson(stmt));
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        throw std::runtime_error("Query step failed: " + std::string(sqlite3_errmsg(db_)));
    }
    return rows;
}

nlohmann::json Database::QueryOne(const std::string& sql, const std::vector<nlohmann::json>& params) {
    auto rows = Query(sql, params);
    return rows.empty() ? nullptr : rows[0];
}

// ─── 事务 ───

void Database::Begin()    { Execute("BEGIN"); }
void Database::Commit()   { Execute("COMMIT"); }
void Database::Rollback() { Execute("ROLLBACK"); }

int64_t Database::LastInsertId() { return sqlite3_last_insert_rowid(db_); }
int     Database::Changes()      { return sqlite3_changes(db_); }

// ─── AI 会话操作 ───

int64_t Database::CreateAIConversation(int64_t user_id, const std::string& title) {
    Execute("INSERT INTO ai_conversations (user_id, title) VALUES (?, ?)", {user_id, title});
    return LastInsertId();
}

nlohmann::json Database::GetAIConversations(int64_t user_id) {
    return Query(
        "SELECT id, title, updated_at, created_at, "
        "(SELECT COUNT(*) FROM ai_messages WHERE conversation_id = ai_conversations.id) AS message_count "
        "FROM ai_conversations WHERE user_id = ? AND is_deleted = 0 ORDER BY updated_at DESC",
        {user_id}
    );
}

nlohmann::json Database::GetAIConversation(int64_t conversation_id, int64_t user_id) {
    return QueryOne(
        "SELECT id, title, updated_at, created_at, "
        "(SELECT COUNT(*) FROM ai_messages WHERE conversation_id = ai_conversations.id) AS message_count "
        "FROM ai_conversations WHERE id = ? AND user_id = ? AND is_deleted = 0",
        {conversation_id, user_id}
    );
}

bool Database::UpdateAIConversation(int64_t conversation_id, int64_t user_id, const std::string& title) {
    Execute(
        "UPDATE ai_conversations SET title = ?, updated_at = datetime('now','localtime') WHERE id = ? AND user_id = ? AND is_deleted = 0",
        {title, conversation_id, user_id}
    );
    return Changes() > 0;
}

bool Database::SoftDeleteAIConversation(int64_t conversation_id, int64_t user_id) {
    Execute(
        "UPDATE ai_conversations SET is_deleted = 1, deleted_at = datetime('now','localtime'), updated_at = datetime('now','localtime') WHERE id = ? AND user_id = ?",
        {conversation_id, user_id}
    );
    return Changes() > 0;
}

// ─── AI 消息操作 ───

int64_t Database::AddAIMessage(int64_t conversation_id, const std::string& role, const std::string& content, int tokens) {
    Execute("INSERT INTO ai_messages (conversation_id, role, content, tokens) VALUES (?, ?, ?, ?)", {conversation_id, role, content, tokens});
    return LastInsertId();
}

nlohmann::json Database::GetAIMessages(int64_t conversation_id, int limit) {
    return Query(
        "SELECT id, role, content, tokens, created_at FROM ai_messages WHERE conversation_id = ? ORDER BY created_at ASC LIMIT ?",
        {conversation_id, limit}
    );
}

void Database::UpdateAIMessageTokens(int64_t message_id, int tokens) {
    Execute("UPDATE ai_messages SET tokens = ? WHERE id = ?", {tokens, message_id});
}

// ─── 内部方法 ───

void Database::BindParams(sqlite3_stmt* stmt, const std::vector<nlohmann::json>& params) {
    for (size_t i = 0; i < params.size(); ++i) {
        int idx = static_cast<int>(i) + 1; // SQLite 参数从 1 开始
        const auto& p = params[i];
        int rc;
        if (p.is_null()) {
            rc = sqlite3_bind_null(stmt, idx);
        } else if (p.is_number_integer()) {
            rc = sqlite3_bind_int64(stmt, idx, p.get<int64_t>());
        } else if (p.is_number_float()) {
            rc = sqlite3_bind_double(stmt, idx, p.get<double>());
        } else if (p.is_string()) {
            auto s = p.get<std::string>();
            rc = sqlite3_bind_text(stmt, idx, s.c_str(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
        } else {
            rc = sqlite3_bind_null(stmt, idx); // 其他类型当 null 处理
        }
        if (rc != SQLITE_OK) {
            sqlite3_finalize(stmt);
            throw std::runtime_error("Bind param " + std::to_string(idx) + " failed: " + std::string(sqlite3_errmsg(db_)));
        }
    }
}

nlohmann::json Database::RowToJson(sqlite3_stmt* stmt) {
    nlohmann::json row;
    int cols = sqlite3_column_count(stmt);
    for (int i = 0; i < cols; ++i) {
        const char* name = sqlite3_column_name(stmt, i);
        switch (sqlite3_column_type(stmt, i)) {
            case SQLITE_INTEGER:
                row[name] = sqlite3_column_int64(stmt, i);
                break;
            case SQLITE_FLOAT:
                row[name] = sqlite3_column_double(stmt, i);
                break;
            case SQLITE_TEXT:
                row[name] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                break;
            case SQLITE_NULL:
                row[name] = nullptr;
                break;
            default:
                row[name] = nullptr;
                break;
        }
    }
    return row;
}

} // namespace mindvault
