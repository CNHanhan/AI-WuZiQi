#pragma once
#include "Types.h"
#include <vector>
#include <utility>

namespace AIWZQ {

extern const int DIRECTIONS[4][2];

// 15x15 五子棋棋盘：0=空 1=黑 2=白
class GomokuBoard {
public:
    GomokuBoard();
    void clear();

    bool placeStone(int x, int y, int color);   // 落子成功返回 true
    bool removeStone(int x, int y);             // AI 搜索用
    void undo();                                 // 撤销最近一步
    int  get(int x, int y) const;

    bool isFull() const;

    // 返回 0=继续 1=黑胜 2=白胜 3=平局
    int  checkWin() const;
    // 判断最后一手是否成五（供增量判定）
    bool isWinAt(int x, int y, int color) const;

    // 候选落点：已有棋子 range 格范围内的空点
    void getValidMoves(std::vector<Point>& moves, int range = 1) const;

    const std::vector<std::pair<int,int>>& history() const { return history_; }

private:
    int grid_[BOARD_SIZE][BOARD_SIZE];
    std::vector<std::pair<int,int>> history_;

    bool inBounds(int x, int y) const { return x >= 0 && x < BOARD_SIZE && y >= 0 && y < BOARD_SIZE; }
    int  countDir(int x, int y, int dx, int dy, int color) const;
};

} // namespace AIWZQ
