#pragma once
/**
 * Room.h — 房间状态机（WAITING / PLAYING / FINISHED）
 * 对应设计文档：01-服务端设计.md 2.4（Room 类：黑白 uid、GomokuBoard、currentTurn、
 *               双方总时限 180s、每步 30s、onPlayerJoin/onPlayerReady/onStonePlaced/onTimeout）
 *               04-数据库设计.md 3.2（对局记录字段）
 *               Types.h（Color / GameEndReason）
 */
#include "GameDefs.h"
#include "GomokuBoard.h"
#include "Types.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace AIWZQ {

// 落子/超时后的处理结果
struct StoneResult {
    bool        ok = false;               // 落子是否被接受
    std::string error;                    // 拒绝原因
    bool        gameOver = false;
    int         winnerColor = Color::EMPTY; // 1=黑 2=白 0=无
    GameEndReason endReason = REASON_FIVE;
    int         totalTimeLeft = 0;        // 当前行棋方剩余总时间
    int         stepTimeLeft = 0;         // 当前步剩余思考时间
};

struct TimeoutResult {
    bool        fired = false;
    int64_t     timeoutUid = 0;
    std::string reason;                   // step_timeout | total_timeout
    int64_t     winnerUid = 0;
};

class Room {
public:
    Room(int64_t roomId, int totalTimeSec = 180, int stepTimeSec = 30,
         std::string password = "");

    int64_t roomId() const { return roomId_; }
    RoomStatus status() const;
    std::string password() const { return password_; }
    bool hasPassword() const { return !password_.empty(); }
    int64_t ownerUid() const { return blackUid_; }

    // 玩家加入；返回是否成功（房间未满且非游戏中）
    bool onPlayerJoin(int64_t uid);
    // 玩家准备；两人均准备后自动进入 PLAYING，黑先手
    bool onPlayerReady(int64_t uid, bool ready = true);
    // 落子（uid 必须是当前行棋方）；返回结果含胜负判定
    StoneResult onStonePlaced(int64_t uid, int x, int y);
    // 超时扫描：返回是否触发超时及胜者
    TimeoutResult onTimeout();

    // 悔棋：回退最近一步（需对方同意由 RoomManager 协调，这里只做棋盘回退与时间回滚一小段）
    bool undoStone(int64_t uid);
    // 和棋 / 认输
    void finishDraw();
    void finishSurrender(int64_t uid);

    // 重置到等待状态（对局结束后复用房间）
    void reset();

    // 生成房间快照（供 room_info_resp / room:{id} hash）
    std::vector<std::pair<std::string, std::string>> snapshot() const;

    // 棋谱 JSON 数组（每个元素 {"x":a,"y":b,"color":c}）
    std::string movesJson() const;

    int64_t blackUid() const { return blackUid_; }
    int64_t whiteUid() const { return whiteUid_; }
    int currentTurn() const { return currentTurn_; }
    int blackTimeLeft() const { return blackTimeLeft_; }
    int whiteTimeLeft() const { return whiteTimeLeft_; }
    int stepTimeLeft() const;
    const GomokuBoard& board() const { return board_; }
    int totalTimeSec() const { return totalTimeSec_; }
    int stepTimeSec() const { return stepTimeSec_; }

    // 是否已两人就位
    bool isFull() const { return blackUid_ > 0 && whiteUid_ > 0; }

private:
    // 扣除行动方思考时间（基于 lastMoveTime_ 的 wall clock 差值）
    void consumeTime_();
    bool isTurnOf_(int64_t uid) const;

    int64_t roomId_;
    int64_t blackUid_ = 0;
    int64_t whiteUid_ = 0;
    RoomStatus status_ = RoomStatus::WAITING;
    GomokuBoard board_;
    int currentTurn_ = Color::BLACK;      // 黑先手
    bool blackReady_ = false;
    bool whiteReady_ = false;

    int totalTimeSec_ = 180;
    int stepTimeSec_ = 30;
    int blackTimeLeft_ = 180;
    int whiteTimeLeft_ = 180;
    std::chrono::steady_clock::time_point turnStart_;
    std::chrono::steady_clock::time_point createdAt_;

    std::string password_;
    mutable std::mutex mtx_;
};

} // namespace AIWZQ
