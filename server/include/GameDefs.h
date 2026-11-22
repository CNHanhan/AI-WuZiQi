#pragma once
/**
 * GameDefs.h — 服务端对局/房间共享枚举
 * 对应设计文档：01-服务端设计.md 2.4（RoomStatus / GameResult）
 *               Types.h（Color / GameEndReason）
 */
#include <cstdint>

namespace AIWZQ {

enum class RoomStatus {
    WAITING = 0,
    PLAYING = 1,
    FINISHED = 2
};

// 对局结果（供 Database::updateUserStats / RoomManager 结算使用）
enum class GameResult {
    NONE = 0,
    BLACK_WIN = 1,
    WHITE_WIN = 2,
    DRAW = 3
};

// 把颜色(1=黑,2=白)转成结果
inline GameResult resultFromWinnerColor(int winnerColor) {
    if (winnerColor == 1) return GameResult::BLACK_WIN;
    if (winnerColor == 2) return GameResult::WHITE_WIN;
    return GameResult::DRAW;
}

} // namespace AIWZQ
