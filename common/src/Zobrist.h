// ============================================================================
// Zobrist.h — AI 内部头：Zobrist 64 位哈希 + 置换表（Transposition Table）
// 对应设计文档：05-AI算法设计.md 5.1 Zobrist 哈希（局面快速去重）
//
// 该头仅在 common/src 内部被 AI 搜索实现包含，不进入公共接口 common/include。
// 纯 C++11，无 Qt 依赖。
// ============================================================================
#pragma once

#include "GomokuBoard.h"   // GomokuBoard / BOARD_SIZE / Color

#include <cstdint>         // uint64_t
#include <unordered_map>   // std::unordered_map

namespace AIWZQ {
namespace AIInternal {

// 5.1：每个棋盘位置、每种颜色对应一个随机 64 位整数。
// zobristTable[x][y][color]；color = 1(黑) / 2(白)，索引 0 不用（留空槽）。
class Zobrist {
public:
    Zobrist()
    {
        // 用局部 xorshift64* 生成随机表：每次运行表一致（碰撞行为可复现），
        // 且不触碰全局 std::rand 状态（简单档随机落子用 std::rand，互不干扰）。
        uint64_t seed = 0x5A17C0DE9E3779B9ull;
        for (int x = 0; x < BOARD_SIZE; ++x) {
            for (int y = 0; y < BOARD_SIZE; ++y) {
                for (int c = 0; c < 3; ++c) {
                    seed = nextRandom(seed);
                    zobristTable[x][y][c] = seed;
                }
            }
        }
    }

    // 全盘哈希：仅对非空交叉点做异或（文档 5.1 原式）。
    uint64_t getBoardHash(const GomokuBoard& board) const
    {
        uint64_t hash = 0;
        for (int x = 0; x < BOARD_SIZE; ++x) {
            for (int y = 0; y < BOARD_SIZE; ++y) {
                const int c = board.get(x, y);
                if (c != Color::EMPTY) {
                    hash ^= zobristTable[x][y][c];
                }
            }
        }
        return hash;
    }

    // 增量哈希：在 hash 上叠加/撤销一子，等价于全盘重算，供搜索热路径使用。
    uint64_t applyMove(uint64_t hash, int x, int y, int color) const
    {
        return hash ^ zobristTable[x][y][color];
    }

private:
    uint64_t zobristTable[BOARD_SIZE][BOARD_SIZE][3];

    // xorshift64*：自包含、线程无关、不依赖全局随机状态。
    static uint64_t nextRandom(uint64_t& state)
    {
        uint64_t x = state;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        state = x;
        return x * 2685821657736338717ull;
    }
};

// 置换表：缓存已评估局面分数（文档 5.1 配 Transposition Table）。
// 键 = Zobrist 哈希，值 = evaluateBoard 的评估结果。
class TranspositionTable {
public:
    TranspositionTable() {}
    ~TranspositionTable() {}

    bool get(uint64_t hash, int& outScore) const
    {
        std::unordered_map<uint64_t, int>::const_iterator it = table_.find(hash);
        if (it == table_.end()) return false;
        outScore = it->second;
        return true;
    }

    void put(uint64_t hash, int score)
    {
        table_[hash] = score;
    }

    void clear()
    {
        table_.clear();
    }

    std::size_t size() const
    {
        return table_.size();
    }

private:
    std::unordered_map<uint64_t, int> table_;
};

} // namespace AIInternal
} // namespace AIWZQ
