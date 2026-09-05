#pragma once
#include "../database.h"
#include <nlohmann/json.hpp>
#include <string>

namespace mindvault::services {

class FlashcardService {
public:
    explicit FlashcardService(Database& db) : db_(db) {}

    // 获取笔记的闪卡
    nlohmann::json GetByNote(int64_t note_id) {
        return db_.Query("SELECT * FROM flashcards WHERE note_id = ? ORDER BY created_at", {note_id});
    }

    // 创建闪卡
    nlohmann::json Create(int64_t note_id, const std::string& front, const std::string& back) {
        db_.Execute("INSERT INTO flashcards (note_id, front, back) VALUES (?, ?, ?)", {note_id, front, back});
        return db_.QueryOne("SELECT * FROM flashcards WHERE id = ?", {db_.LastInsertId()});
    }

    // 更新闪卡
    nlohmann::json Update(int64_t id, const std::string& front, const std::string& back) {
        db_.Execute("UPDATE flashcards SET front = ?, back = ? WHERE id = ?", {front, back, id});
        return db_.QueryOne("SELECT * FROM flashcards WHERE id = ?", {id});
    }

    // 删除闪卡
    bool Delete(int64_t id) {
        db_.Execute("DELETE FROM flashcards WHERE id = ?", {id});
        return db_.Changes() > 0;
    }

    // 获取待复习闪卡
    nlohmann::json GetDue() {
        return db_.Query(R"(
            SELECT f.*, n.title as note_title
            FROM flashcards f
            JOIN notes n ON f.note_id = n.id
            WHERE f.next_review IS NULL OR f.next_review <= datetime('now','localtime')
            ORDER BY f.next_review
            LIMIT 50
        )");
    }

    // 提交复习结果（SM-2 算法简化版）
    nlohmann::json Review(int64_t id, int quality) {
        auto card = db_.QueryOne("SELECT * FROM flashcards WHERE id = ?", {id});
        if (card.is_null()) return nullptr;

        double ef = card.value("ease_factor", 2.5);
        int interval = card.value("interval_days", 0);

        // SM-2 算法
        if (quality < 3) {
            // 忘记了，重置间隔
            interval = 1;
        } else {
            // 记住了，增加间隔
            if (interval == 0) interval = 1;
            else if (interval == 1) interval = 6;
            else interval = (int)(interval * ef);
        }

        // 更新 ease_factor
        ef = ef + (0.1 - (5 - quality) * (0.08 + (5 - quality) * 0.02));
        if (ef < 1.3) ef = 1.3;

        // 更新数据库
        db_.Execute(R"(
            UPDATE flashcards
            SET ease_factor = ?, interval_days = ?, next_review = datetime('now','localtime','+' || ? || ' days')
            WHERE id = ?
        )", {ef, interval, interval, id});

        return db_.QueryOne("SELECT * FROM flashcards WHERE id = ?", {id});
    }

private:
    Database& db_;
};

}
