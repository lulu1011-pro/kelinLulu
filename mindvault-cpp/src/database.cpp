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

void Database::Init() {
    // ─── 笔记表 ───
    Execute(R"(
        CREATE TABLE IF NOT EXISTS notes (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
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

    // ─── FTS5 全文搜索虚拟表 ───
    // 使用独立存储模式（非 content=notes），更可靠
    Execute(R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts
        USING fts5(title, content)
    )");

    // ─── FTS5 同步触发器 ───
    // INSERT 时同步
    Execute(R"(
        CREATE TRIGGER IF NOT EXISTS notes_ai AFTER INSERT ON notes BEGIN
            INSERT INTO notes_fts(rowid, title, content)
            VALUES (new.id, new.title, new.content);
        END
    )");
    // UPDATE 时同步
    Execute(R"(
        CREATE TRIGGER IF NOT EXISTS notes_au AFTER UPDATE ON notes BEGIN
            INSERT INTO notes_fts(notes_fts, rowid, title, content)
            VALUES ('delete', old.id, old.title, old.content);
            INSERT INTO notes_fts(rowid, title, content)
            VALUES (new.id, new.title, new.content);
        END
    )");
    // DELETE 时同步
    Execute(R"(
        CREATE TRIGGER IF NOT EXISTS notes_ad AFTER DELETE ON notes BEGIN
            INSERT INTO notes_fts(notes_fts, rowid, title, content)
            VALUES ('delete', old.id, old.title, old.content);
        END
    )");

    std::cout << "[DB] Schema initialized (notes, tags, note_tags, link_edges, notes_fts)" << std::endl;
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
        throw std::runtime_error("Prepare failed: " + std::string(sqlite3_errmsg(db_)));
    }
    BindParams(stmt, params);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("Execute failed: " + std::string(sqlite3_errmsg(db_)));
    }
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
