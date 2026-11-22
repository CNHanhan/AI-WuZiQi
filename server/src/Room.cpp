/**
 * Room.cpp — 房间状态机实现
 * 对应设计文档：01-服务端设计.md 2.4；Types.h（Color / GameEndReason）
 */
#include "Room.h"

#include <algorithm>
#include <sstream>

namespace AIWZQ {

Room::Room(int64_t roomId, int totalTimeSec, int stepTimeSec, std::string password)
    : roomId_(roomId), totalTimeSec_(totalTimeSec), stepTimeSec_(stepTimeSec),
      blackTimeLeft_(totalTimeSec), whiteTimeLeft_(totalTimeSec),
      turnStart_(std::chrono::steady_clock::now()),
      createdAt_(std::chrono::steady_clock::now()),
      password_(std::move(password)) {}

RoomStatus Room::status() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return status_;
}

bool Room::onPlayerJoin(int64_t uid) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (status_ != RoomStatus::WAITING) return false;
    if (uid <= 0) return false;
    if (blackUid_ == uid || whiteUid_ == uid) return true; // 重入视为已在房间
    if (blackUid_ == 0) { blackUid_ = uid; return true; }
    if (whiteUid_ == 0) { whiteUid_ = uid; return true; }
    return false; // 已满
}

bool Room::onPlayerReady(int64_t uid, bool ready) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (status_ != RoomStatus::WAITING) return false;
    if (blackUid_ == uid) blackReady_ = ready;
    else if (whiteUid_ == uid) whiteReady_ = ready;
    else return false;

    if (blackUid_ > 0 && whiteUid_ > 0 && blackReady_ && whiteReady_) {
        status_ = RoomStatus::PLAYING;
        currentTurn_ = Color::BLACK;
        blackTimeLeft_ = totalTimeSec_;
        whiteTimeLeft_ = totalTimeSec_;
        board_.clear();
        turnStart_ = std::chrono::steady_clock::now();
    }
    return true;
}

bool Room::isTurnOf_(int64_t uid) const {
    if (currentTurn_ == Color::BLACK) return uid == blackUid_;
    return uid == whiteUid_;
}

void Room::consumeTime_() {
    const auto now = std::chrono::steady_clock::now();
    const int elapsed = static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(now - turnStart_).count());
    if (elapsed <= 0) return;
    if (currentTurn_ == Color::BLACK) blackTimeLeft_ = std::max(0, blackTimeLeft_ - elapsed);
    else                              whiteTimeLeft_ = std::max(0, whiteTimeLeft_ - elapsed);
    turnStart_ = now;
}

int Room::stepTimeLeft() const {
    auto self = const_cast<Room*>(this);
    const auto now = std::chrono::steady_clock::now();
    const int elapsed = static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(now - turnStart_).count());
    return std::max(0, self->stepTimeSec_ - elapsed);
}

StoneResult Room::onStonePlaced(int64_t uid, int x, int y) {
    std::lock_guard<std::mutex> lk(mtx_);
    StoneResult r;
    if (status_ != RoomStatus::PLAYING) { r.error = "room_not_playing"; return r; }
    if (!isTurnOf_(uid)) { r.error = "not_your_turn"; return r; }
    if (!Point(x, y).valid()) { r.error = "invalid_coordinate"; return r; }
    if (board_.get(x, y) != Color::EMPTY) { r.error = "cell_occupied"; return r; }

    consumeTime_();
    // 每步时限判定（超时由 onTimeout 主动扫描，这里兜底）
    const auto now = std::chrono::steady_clock::now();
    const int elapsed = static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(now - turnStart_).count());
    if (elapsed > stepTimeSec_) { r.error = "step_timeout"; return r; }

    const int color = (uid == blackUid_) ? Color::BLACK : Color::WHITE;
    if (!board_.placeStone(x, y, color)) { r.error = "place_failed"; return r; }

    r.ok = true;
    const int win = board_.checkWin();
    if (win != 0) {
        r.gameOver = true;
        r.winnerColor = (win == 1 || win == 2) ? win : Color::EMPTY;
        r.endReason = (win == 3) ? REASON_DRAW : REASON_FIVE;
        status_ = RoomStatus::FINISHED;
    } else if (board_.isFull()) {
        r.gameOver = true;
        r.winnerColor = Color::EMPTY;
        r.endReason = REASON_DRAW;
        status_ = RoomStatus::FINISHED;
    } else {
        currentTurn_ = (currentTurn_ == Color::BLACK) ? Color::WHITE : Color::BLACK;
        turnStart_ = now;
    }
    r.totalTimeLeft = (currentTurn_ == Color::BLACK) ? blackTimeLeft_ : whiteTimeLeft_;
    r.stepTimeLeft = stepTimeLeft();
    return r;
}

TimeoutResult Room::onTimeout() {
    std::lock_guard<std::mutex> lk(mtx_);
    TimeoutResult r;
    if (status_ != RoomStatus::PLAYING) return r;

    const auto now = std::chrono::steady_clock::now();
    const int stepElapsed = static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(now - turnStart_).count());
    const int totalLeft = (currentTurn_ == Color::BLACK) ? blackTimeLeft_ : whiteTimeLeft_;

    // 先判定总时限（更严重），再判定单步超时
    bool stepTimeout = stepElapsed > stepTimeSec_;
    bool totalTimeout = false;
    if (stepTimeout) {
        consumeTime_();
        const int left = (currentTurn_ == Color::BLACK) ? blackTimeLeft_ : whiteTimeLeft_;
        totalTimeout = (left <= 0);
    } else if (totalLeft <= 0) {
        totalTimeout = true;
    }

    if (!stepTimeout && !totalTimeout) return r;

    r.fired = true;
    r.timeoutUid = (currentTurn_ == Color::BLACK) ? blackUid_ : whiteUid_;
    r.reason = totalTimeout ? "total_timeout" : "step_timeout";
    r.winnerUid = (currentTurn_ == Color::BLACK) ? whiteUid_ : blackUid_;
    status_ = RoomStatus::FINISHED;
    return r;
}

bool Room::undoStone(int64_t uid) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (status_ != RoomStatus::PLAYING) return false;
    if (board_.history().empty()) return false;
    // 只能回退到对手刚落的那一步之后（即回到自己回合）
    board_.undo();
    currentTurn_ = (uid == blackUid_) ? Color::BLACK : Color::WHITE;
    turnStart_ = std::chrono::steady_clock::now();
    // 悔棋补偿少量时间（避免被滥用拖时间）
    if (currentTurn_ == Color::BLACK) blackTimeLeft_ = std::min(totalTimeSec_, blackTimeLeft_ + stepTimeSec_);
    else                              whiteTimeLeft_ = std::min(totalTimeSec_, whiteTimeLeft_ + stepTimeSec_);
    return true;
}

void Room::finishDraw() {
    std::lock_guard<std::mutex> lk(mtx_);
    status_ = RoomStatus::FINISHED;
}

void Room::finishSurrender(int64_t uid) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (status_ != RoomStatus::PLAYING) return;
    status_ = RoomStatus::FINISHED;
    (void)uid; // 胜者在 RoomManager 依据认输方反推
}

void Room::reset() {
    std::lock_guard<std::mutex> lk(mtx_);
    status_ = RoomStatus::WAITING;
    board_.clear();
    blackUid_ = 0; whiteUid_ = 0;
    blackReady_ = whiteReady_ = false;
    currentTurn_ = Color::BLACK;
    blackTimeLeft_ = totalTimeSec_;
    whiteTimeLeft_ = totalTimeSec_;
    turnStart_ = std::chrono::steady_clock::now();
}

std::string Room::movesJson() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::ostringstream os;
    os << "[";
    bool first = true;
    int idx = 0;
    for (const auto& mv : board_.history()) {
        if (!first) os << ",";
        first = false;
        // 颜色由落子顺序交替推定（黑先）
        const int color = (idx++ % 2 == 0) ? Color::BLACK : Color::WHITE;
        os << "{\"x\":" << mv.first << ",\"y\":" << mv.second
           << ",\"color\":" << color << "}";
    }
    os << "]";
    return os.str();
}

std::vector<std::pair<std::string, std::string>> Room::snapshot() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return {
        {"room_id", std::to_string(roomId_)},
        {"status", status_ == RoomStatus::WAITING ? "waiting" :
                   status_ == RoomStatus::PLAYING ? "playing" : "finished"},
        {"black_uid", std::to_string(blackUid_)},
        {"white_uid", std::to_string(whiteUid_)},
        {"current_turn", std::to_string(currentTurn_)},
        {"black_time_left", std::to_string(blackTimeLeft_)},
        {"white_time_left", std::to_string(whiteTimeLeft_)},
        {"has_password", hasPassword() ? "1" : "0"}
    };
}

} // namespace AIWZQ
