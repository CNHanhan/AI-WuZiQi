// 离线 AI 引擎三档难度测试
// 编译：g++ -std=c++14 -I../common/include ai_test.cpp ../server/build-win/aiwzq_common/libAIWZQ_Common.a -lws2_32 -o ai_test.exe
#include "GomokuAI.h"
#include "GomokuBoard.h"
#include <iostream>
#include <chrono>

using namespace AIWZQ;

static bool testDifficulty(int diff, const char* name) {
    GomokuAI ai(diff);
    GomokuBoard board;
    // 造一个局面：AI(黑) 在 (7,5)(7,6)(7,7)，玩家(白)在 (5,5)(5,6)
    board.placeStone(7, 5, Color::BLACK);
    board.placeStone(7, 6, Color::BLACK);
    board.placeStone(7, 7, Color::BLACK);
    board.placeStone(5, 5, Color::WHITE);
    board.placeStone(5, 6, Color::WHITE);

    auto t0 = std::chrono::steady_clock::now();
    Point p = ai.getMove(board, Color::BLACK);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    bool ok = p.valid() && board.get(p.x, p.y) == Color::EMPTY;
    std::cout << "[" << (ok ? "PASS" : "FAIL") << "] " << name
              << " 落子=(" << p.x << "," << p.y << ") 耗时=" << (int)ms << "ms" << std::endl;
    return ok;
}

int main() {
    std::cout << "=== AI 引擎三档难度测试 ===" << std::endl;
    bool all = true;
    all &= testDifficulty(1, "简单(贪心)");
    all &= testDifficulty(2, "中等(Alpha-Beta)");
    all &= testDifficulty(3, "困难(MCTS)");
    std::cout << "=== " << (all ? "ALL PASS" : "SOME FAILED") << " ===" << std::endl;
    return all ? 0 : 1;
}
