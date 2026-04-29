// Prevents additional console window on Windows in release, DO NOT REMOVE!!
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
use tauri::Manager;

// ============ 数据结构 ============

#[derive(Debug, Serialize, Deserialize, Clone)]
struct Note {
    id: String,
    vault_id: String,
    file_path: String,
    title: String,
    tags: String,          // JSON数组
    created_at: String,
    modified_at: String,
    is_deleted: i32,
}

#[derive(Debug, Serialize, Deserialize, Clone)]
struct Vault {
    id: String,
    name: String,
    path: String,
    created_at: String,
}

#[derive(Debug, Serialize, Deserialize)]
struct CreateNotePayload {
    vault_id: String,
    title: String,
    folder: Option<String>,
}

#[derive(Debug, Serialize, Deserialize)]
struct SearchPayload {
    vault_id: String,
    query: String,
}

// ============ 文件操作命令 ============

#[tauri::command]
fn create_note(app: tauri::AppHandle, payload: CreateNotePayload) -> Result<Note, String> {
    let vaults_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    let db_path = vaults_dir.join("mindvault.db");

    // 获取知识库路径
    let vault_path = get_vault_path(&db_path, &payload.vault_id)?;

    // 确定文件路径
    let folder = payload.folder.unwrap_or_default();
    let dir_path = if folder.is_empty() {
        PathBuf::from(&vault_path)
    } else {
        PathBuf::from(&vault_path).join(&folder)
    };

    // 确保目录存在
    fs::create_dir_all(&dir_path).map_err(|e| e.to_string())?;

    // 创建文件名（安全处理）
    let safe_name = payload.title.replace(['<', '>', ':', '"', '/', '\\', '|', '?', '*'], "_");
    let file_path = dir_path.join(format!("{}.md", safe_name));

    // 写入初始内容
    let content = format!("# {}\n\n", payload.title);
    fs::write(&file_path, &content).map_err(|e| e.to_string())?;

    // 获取相对路径
    let relative_path = file_path
        .strip_prefix(&vault_path)
        .map_err(|e| e.to_string())?
        .to_str()
        .ok_or("路径编码错误")?
        .to_string();

    let now = chrono::Local::now().format("%Y-%m-%d %H:%M:%S").to_string();
    let id = uuid::Uuid::new_v4().to_string();

    let note = Note {
        id: id.clone(),
        vault_id: payload.vault_id.clone(),
        file_path: relative_path,
        title: payload.title.clone(),
        tags: "[]".to_string(),
        created_at: now.clone(),
        modified_at: now,
        is_deleted: 0,
    };

    // 写入数据库
    let conn = get_db_connection(&db_path)?;
    conn.execute(
        "INSERT INTO notes (id, vault_id, file_path, title, tags, created_at, modified_at, is_deleted) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)",
        [&note.id, &note.vault_id, &note.file_path, &note.title, &note.tags, &note.created_at, &note.modified_at, &note.is_deleted.to_string()],
    ).map_err(|e| e.to_string())?;

    // 更新FTS索引
    conn.execute(
        "INSERT INTO notes_fts (note_id, title, content, tags) VALUES (?1, ?2, ?3, ?4)",
        [&note.id, &note.title, &content, &note.tags],
    ).map_err(|e| e.to_string())?;

    Ok(note)
}

#[tauri::command]
fn read_note(app: tauri::AppHandle, note_id: String) -> Result<String, String> {
    let vaults_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    let db_path = vaults_dir.join("mindvault.db");

    // 获取笔记信息
    let conn = get_db_connection(&db_path)?;
    let note: Note = conn.query_row(
        "SELECT * FROM notes WHERE id = ?1 AND is_deleted = 0",
        [&note_id],
        |row| {
            Ok(Note {
                id: row.get(0)?,
                vault_id: row.get(1)?,
                file_path: row.get(2)?,
                title: row.get(3)?,
                tags: row.get(4)?,
                created_at: row.get(5)?,
                modified_at: row.get(6)?,
                is_deleted: row.get(7)?,
            })
        },
    ).map_err(|e| e.to_string())?;

    // 获取知识库路径
    let vault_path = get_vault_path(&db_path, &note.vault_id)?;

    // 读取文件内容
    let full_path = PathBuf::from(&vault_path).join(&note.file_path);
    let content = fs::read_to_string(&full_path).map_err(|e| e.to_string())?;

    Ok(content)
}

#[tauri::command]
fn save_note(app: tauri::AppHandle, note_id: String, content: String) -> Result<(), String> {
    let vaults_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    let db_path = vaults_dir.join("mindvault.db");

    let conn = get_db_connection(&db_path)?;

    // 获取笔记信息
    let note: Note = conn.query_row(
        "SELECT * FROM notes WHERE id = ?1 AND is_deleted = 0",
        [&note_id],
        |row| {
            Ok(Note {
                id: row.get(0)?,
                vault_id: row.get(1)?,
                file_path: row.get(2)?,
                title: row.get(3)?,
                tags: row.get(4)?,
                created_at: row.get(5)?,
                modified_at: row.get(6)?,
                is_deleted: row.get(7)?,
            })
        },
    ).map_err(|e| e.to_string())?;

    // 写入文件
    let vault_path = get_vault_path(&db_path, &note.vault_id)?;
    let full_path = PathBuf::from(&vault_path).join(&note.file_path);
    fs::write(&full_path, &content).map_err(|e| e.to_string())?;

    // 更新数据库
    let now = chrono::Local::now().format("%Y-%m-%d %H:%M:%S").to_string();
    conn.execute(
        "UPDATE notes SET modified_at = ?1 WHERE id = ?2",
        [&now, &note_id],
    ).map_err(|e| e.to_string())?;

    // 更新FTS索引
    conn.execute(
        "UPDATE notes_fts SET content = ?1 WHERE note_id = ?2",
        [&content, &note_id],
    ).map_err(|e| e.to_string())?;

    Ok(())
}

#[tauri::command]
fn delete_note(app: tauri::AppHandle, note_id: String) -> Result<(), String> {
    let vaults_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    let db_path = vaults_dir.join("mindvault.db");

    let conn = get_db_connection(&db_path)?;

    // 软删除
    conn.execute(
        "UPDATE notes SET is_deleted = 1 WHERE id = ?1",
        [&note_id],
    ).map_err(|e| e.to_string())?;

    // 从FTS删除
    conn.execute(
        "DELETE FROM notes_fts WHERE note_id = ?1",
        [&note_id],
    ).map_err(|e| e.to_string())?;

    Ok(())
}

// ============ 知识库操作 ============

#[tauri::command]
fn create_vault(app: tauri::AppHandle, name: String, path: String) -> Result<Vault, String> {
    let vaults_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    fs::create_dir_all(&vaults_dir).map_err(|e| e.to_string())?;

    let db_path = vaults_dir.join("mindvault.db");

    // 创建知识库目录
    fs::create_dir_all(&path).map_err(|e| e.to_string())?;

    let id = uuid::Uuid::new_v4().to_string();
    let now = chrono::Local::now().format("%Y-%m-%d %H:%M:%S").to_string();

    let vault = Vault {
        id: id.clone(),
        name: name.clone(),
        path: path.clone(),
        created_at: now,
    };

    let conn = get_db_connection(&db_path)?;
    conn.execute(
        "INSERT INTO vaults (id, name, path, created_at) VALUES (?1, ?2, ?3, ?4)",
        [&vault.id, &vault.name, &vault.path, &vault.created_at],
    ).map_err(|e| e.to_string())?;

    Ok(vault)
}

#[tauri::command]
fn get_vaults(app: tauri::AppHandle) -> Result<Vec<Vault>, String> {
    let vaults_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    let db_path = vaults_dir.join("mindvault.db");

    let conn = get_db_connection(&db_path)?;
    let mut stmt = conn.prepare("SELECT * FROM vaults").map_err(|e| e.to_string())?;

    let vaults = stmt.query_map([], |row| {
        Ok(Vault {
            id: row.get(0)?,
            name: row.get(1)?,
            path: row.get(2)?,
            created_at: row.get(3)?,
        })
    }).map_err(|e| e.to_string())?
    .filter_map(|v| v.ok())
    .collect();

    Ok(vaults)
}

#[tauri::command]
fn get_notes(app: tauri::AppHandle, vault_id: String) -> Result<Vec<Note>, String> {
    let vaults_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    let db_path = vaults_dir.join("mindvault.db");

    let conn = get_db_connection(&db_path)?;
    let mut stmt = conn.prepare(
        "SELECT * FROM notes WHERE vault_id = ?1 AND is_deleted = 0 ORDER BY modified_at DESC"
    ).map_err(|e| e.to_string())?;

    let notes = stmt.query_map([&vault_id], |row| {
        Ok(Note {
            id: row.get(0)?,
            vault_id: row.get(1)?,
            file_path: row.get(2)?,
            title: row.get(3)?,
            tags: row.get(4)?,
            created_at: row.get(5)?,
            modified_at: row.get(6)?,
            is_deleted: row.get(7)?,
        })
    }).map_err(|e| e.to_string())?
    .filter_map(|n| n.ok())
    .collect();

    Ok(notes)
}

// ============ 全文检索 ============

#[derive(Debug, Serialize)]
struct SearchResult {
    id: String,
    title: String,
    file_path: String,
    snippet: String,
    rank: f64,
}

#[tauri::command]
fn search_notes(app: tauri::AppHandle, payload: SearchPayload) -> Result<Vec<SearchResult>, String> {
    let vaults_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    let db_path = vaults_dir.join("mindvault.db");

    let conn = get_db_connection(&db_path)?;

    // 构建FTS查询
    let terms: Vec<String> = payload.query.split_whitespace()
        .filter(|t| !t.is_empty())
        .map(|t| format!("\"{}\"*", t))
        .collect();
    let fts_query = terms.join(" AND ");

    if fts_query.is_empty() {
        return Ok(vec![]);
    }

    let mut stmt = conn.prepare(
        "SELECT n.id, n.title, n.file_path, substr(f.content, 1, 200) as snippet, f.rank \
         FROM notes_fts f \
         JOIN notes n ON n.id = f.note_id \
         WHERE notes_fts MATCH ?1 AND n.vault_id = ?2 AND n.is_deleted = 0 \
         ORDER BY f.rank \
         LIMIT 50"
    ).map_err(|e| e.to_string())?;

    let results = stmt.query_map([&fts_query, &payload.vault_id], |row| {
        Ok(SearchResult {
            id: row.get(0)?,
            title: row.get(1)?,
            file_path: row.get(2)?,
            snippet: row.get(3)?,
            rank: row.get(4)?,
        })
    }).map_err(|e| e.to_string())?
    .filter_map(|r| r.ok())
    .collect();

    Ok(results)
}

// ============ 双向链接 ============

#[derive(Debug, Serialize)]
struct Backlink {
    id: String,
    source_id: String,
    source_title: String,
    target_title: String,
    context: String,
}

#[tauri::command]
fn get_backlinks(app: tauri::AppHandle, note_title: String) -> Result<Vec<Backlink>, String> {
    let vaults_dir = app.path().app_data_dir().map_err(|e| e.to_string())?;
    let db_path = vaults_dir.join("mindvault.db");

    let conn = get_db_connection(&db_path)?;
    let mut stmt = conn.prepare(
        "SELECT l.id, l.source_id, n.title as source_title, l.target_title, '' as context \
         FROM links l \
         JOIN notes n ON l.source_id = n.id \
         WHERE l.target_title = ?1 AND n.is_deleted = 0"
    ).map_err(|e| e.to_string())?;

    let results = stmt.query_map([&note_title], |row| {
        Ok(Backlink {
            id: row.get(0)?,
            source_id: row.get(1)?,
            source_title: row.get(2)?,
            target_title: row.get(3)?,
            context: row.get(4)?,
        })
    }).map_err(|e| e.to_string())?
    .filter_map(|r| r.ok())
    .collect();

    Ok(results)
}

// ============ 辅助函数 ============

fn get_db_connection(db_path: &Path) -> Result<rusqlite::Connection, String> {
    let conn = rusqlite::Connection::open(db_path).map_err(|e| e.to_string())?;
    init_db(&conn)?;
    Ok(conn)
}

fn init_db(conn: &rusqlite::Connection) -> Result<(), String> {
    conn.execute_batch(
        "CREATE TABLE IF NOT EXISTS vaults (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            path TEXT NOT NULL UNIQUE,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS notes (
            id TEXT PRIMARY KEY,
            vault_id TEXT NOT NULL,
            file_path TEXT NOT NULL,
            title TEXT NOT NULL,
            tags TEXT DEFAULT '[]',
            created_at DATETIME,
            modified_at DATETIME,
            is_deleted INTEGER DEFAULT 0,
            FOREIGN KEY (vault_id) REFERENCES vaults(id)
        );

        CREATE INDEX IF NOT EXISTS idx_notes_vault ON notes(vault_id);
        CREATE INDEX IF NOT EXISTS idx_notes_modified ON notes(modified_at DESC);

        CREATE TABLE IF NOT EXISTS links (
            id TEXT PRIMARY KEY,
            source_id TEXT NOT NULL,
            target_id TEXT,
            target_title TEXT NOT NULL,
            link_type TEXT DEFAULT 'page',
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (source_id) REFERENCES notes(id) ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_links_source ON links(source_id);
        CREATE INDEX IF NOT EXISTS idx_links_target ON links(target_title);

        CREATE TABLE IF NOT EXISTS tags (
            id TEXT PRIMARY KEY,
            vault_id TEXT NOT NULL,
            name TEXT NOT NULL,
            parent_id TEXT,
            color TEXT,
            FOREIGN KEY (vault_id) REFERENCES vaults(id) ON DELETE CASCADE
        );

        CREATE VIRTUAL TABLE IF NOT EXISTS notes_fts USING fts5(
            note_id UNINDEXED,
            title,
            content,
            tags,
            content='',
            tokenize='unicode61 remove_diacritics 2'
        );
        "
    ).map_err(|e| e.to_string())?;

    Ok(())
}

fn get_vault_path(db_path: &Path, vault_id: &str) -> Result<String, String> {
    let conn = rusqlite::Connection::open(db_path).map_err(|e| e.to_string())?;
    let path: String = conn.query_row(
        "SELECT path FROM vaults WHERE id = ?1",
        [vault_id],
        |row| row.get(0),
    ).map_err(|e| e.to_string())?;
    Ok(path)
}

// ============ 主函数 ============

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_shell::init())
        .plugin(tauri_plugin_fs::init())
        .plugin(tauri_plugin_dialog::init())
        .setup(|app| {
            // 确保数据目录存在
            let vaults_dir = app.path().app_data_dir()?;
            fs::create_dir_all(&vaults_dir)?;

            // 初始化数据库
            let db_path = vaults_dir.join("mindvault.db");
            let conn = rusqlite::Connection::open(&db_path)?;
            init_db(&conn)?;
            drop(conn);

            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            create_note,
            read_note,
            save_note,
            delete_note,
            create_vault,
            get_vaults,
            get_notes,
            search_notes,
            get_backlinks,
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
