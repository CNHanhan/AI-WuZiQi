#pragma once
#include "GomokuBoard.h"

namespace AIWZQ {

// 离线 AI 引擎：三档难度，纯 C++，服务端与客户端共用
// 实现见 common/src/GomokuAI.cpp、Evaluator.cpp、MCTS.cpp
class GomokuAI {
public:
    // difficulty: 1=简单(贪心+威胁检测) 2=中等(Alpha-Beta 4~6层) 3=困难(MCTS 1w~5w次)
    explicit GomokuAI(int difficulty = 2);
    ~GomokuAI();

    // 返回落子点；无合法点返回 Point(-1,-1)
    Point getMove(const GomokuBoard& board, int aiColor);

    // 可选：限制思考时间（毫秒），0=不限制；迭代深化用它做超时控制
    void setTimeLimitMs(int ms);

    int difficulty() const { return difficulty_; }

private:
    struct Impl;
    Impl* impl_;
    int difficulty_;
};

// 棋型评估器（AI 内部使用，也供单元测试）
class Evaluator {
public:
    // 返回 aiColor 视角的局面分（正=AI优，负=对手优）
    static int evaluateBoard(const GomokuBoard& board, int aiColor);
    // 落子后该点的进攻/防守增益
    static int pointScore(const GomokuBoard& board, int x, int y, int color);
};

} // namespace AIWZQ
