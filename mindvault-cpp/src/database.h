#pragma once
// MindVault SQLite3 数据库封装层
// 基于 SQLite C API，支持参数化查询、事务管理

#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include <memory>
#include <filesystem>

namespace mindvault {

class Database {
public:
    explicit Database(const std::string& db_path);
    explicit Database(const std::filesystem::path& db_path);
    ~Database();

    // 禁止拷贝
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // 初始化：建表 + FTS5 索引
    void Init();

    // 执行单条 SQL（无返回值，如 INSERT/UPDATE/DELETE）
    void Execute(const std::string& sql);

    // 参数化执行（防 SQL 注入）
    // params 支持类型：int, int64_t, double, std::string, nullptr
    void Execute(const std::string& sql, const std::vector<nlohmann::json>& params);

    // 查询，返回 JSON 数组
    // 每行是一个 {列名: 值} 的 JSON 对象
    nlohmann::json Query(const std::string& sql, const std::vector<nlohmann::json>& params = {});

    // 查询单行
    nlohmann::json QueryOne(const std::string& sql, const std::vector<nlohmann::json>& params = {});

    // 事务
    void Begin();
    void Commit();
    void Rollback();

    // 获取最后插入的 rowid
    int64_t LastInsertId();

    // 获取影响的行数
    int Changes();

    // 获取原生句柄（高级用法）
    sqlite3* Handle() { return db_; }

    // ─── AI 会话操作 ───
    int64_t CreateAIConversation(int64_t user_id, const std::string& title = "新对话");
    nlohmann::json GetAIConversations(int64_t user_id);
    nlohmann::json GetAIConversation(int64_t conversation_id, int64_t user_id);
    bool UpdateAIConversation(int64_t conversation_id, int64_t user_id, const std::string& title);
    bool SoftDeleteAIConversation(int64_t conversation_id, int64_t user_id);

    // ─── AI 消息操作 ───
    int64_t AddAIMessage(int64_t conversation_id, const std::string& role, const std::string& content, int tokens = 0);
    nlohmann::json GetAIMessages(int64_t conversation_id, int limit = 50);
    void UpdateAIMessageTokens(int64_t message_id, int tokens);

    // ─── 笔记切块（P1-5 混合检索用）───
    int64_t AddChunk(int64_t note_id, int chunk_index, const std::string& chunk_text, const std::string& embedding_json);
    nlohmann::json GetChunksByNote(int64_t note_id);
    void DeleteChunksByNote(int64_t note_id);
    nlohmann::json GetAllChunks();

private:
    sqlite3* db_ = nullptr;

    // 绑定参数到 prepared statement
    void BindParams(sqlite3_stmt* stmt, const std::vector<nlohmann::json>& params);

    // 将一行结果转为 JSON
    nlohmann::json RowToJson(sqlite3_stmt* stmt);
};

} // namespace mindvault
