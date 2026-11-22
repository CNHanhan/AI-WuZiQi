// ============================================================================
// GomokuBoard.cpp — 15x15 五子棋棋盘公共基础实现
// 对应设计文档：
//   * 05-AI算法设计.md  二、棋盘表示（0=空/1=黑/2=白、DIRECTIONS、落子候选点）
//   * 05-AI算法设计.md  四、搜索算法（增量胜负判定 isWinAt）
//   * 03-通信协议设计.md 7.4 坐标验证（0-14）
// 说明：common 为服务端/客户端共用基础库，不引入任何 Qt 依赖。
// ============================================================================

#include "GomokuBoard.h"   // common/include

#include <algorithm>       // std::find

namespace AIWZQ {

// 05-AI算法设计.md 二、棋盘表示：四个判定方向（横/纵/右斜/左斜）。
// 头文件仅做 extern 声明，实体在此 .cpp 定义，供 AI 模块与棋子逻辑共用。
const int DIRECTIONS[4][2] = {
    {1,  0},   // 横向 →
    {0,  1},   // 纵向 ↓
    {1,  1},   // 右斜 ↘
    {1, -1}    // 左斜 ↗
};

GomokuBoard::GomokuBoard()
{
    clear();
}

void GomokuBoard::clear()
{
    for (int x = 0; x < BOARD_SIZE; ++x) {
        for (int y = 0; y < BOARD_SIZE; ++y) {
            grid_[x][y] = Color::EMPTY;
        }
    }
    history_.clear();
}

bool GomokuBoard::placeStone(int x, int y, int color)
{
    if (!inBounds(x, y))            return false;
    if (color != Color::BLACK && color != Color::WHITE) return false;
    if (grid_[x][y] != Color::EMPTY) return false;

    grid_[x][y] = color;
    history_.push_back(std::make_pair(x, y));
    return true;
}

bool GomokuBoard::removeStone(int x, int y)
{
    if (!inBounds(x, y))            return false;
    if (grid_[x][y] == Color::EMPTY) return false;

    grid_[x][y] = Color::EMPTY;

    // AI 搜索时以 removeStone 回溯，history_ 必须同步移除该点，
    // 否则后续 undo() 会把已删除的棋子状态弄脏。取最后一个匹配项。
    for (std::vector<std::pair<int, int> >::reverse_iterator it = history_.rbegin();
         it != history_.rend(); ++it) {
        if (it->first == x && it->second == y) {
            history_.erase((it + 1).base());
            break;
        }
    }
    return true;
}

void GomokuBoard::undo()
{
    if (history_.empty()) return;

    const std::pair<int, int> last = history_.back();
    history_.pop_back();
    if (inBounds(last.first, last.second)) {
        grid_[last.first][last.second] = Color::EMPTY;
    }
}

int GomokuBoard::get(int x, int y) const
{
    if (!inBounds(x, y)) return Color::EMPTY;
    return grid_[x][y];
}

bool GomokuBoard::isFull() const
{
    for (int x = 0; x < BOARD_SIZE; ++x) {
        for (int y = 0; y < BOARD_SIZE; ++y) {
            if (grid_[x][y] == Color::EMPTY) return false;
        }
    }
    return true;
}

// 沿 (dx,dy) 单方向连续同色子数（不含起点 (x,y)）。
int GomokuBoard::countDir(int x, int y, int dx, int dy, int color) const
{
    int count = 0;
    int nx = x + dx;
    int ny = y + dy;
    while (inBounds(nx, ny) && grid_[nx][ny] == color) {
        ++count;
        nx += dx;
        ny += dy;
    }
    return count;
}

bool GomokuBoard::isWinAt(int x, int y, int color) const
{
    if (!inBounds(x, y)) return false;
    if (color != Color::BLACK && color != Color::WHITE) return false;

    // 增量判定：最后一手在 (x,y)，四个方向各自「两侧连续数 + 自身 1」，
    // 任一方向 >= 5 即成五。
    for (int d = 0; d < 4; ++d) {
        const int dx = DIRECTIONS[d][0];
        const int dy = DIRECTIONS[d][1];
        const int line = 1
                       + countDir(x, y,  dx,  dy, color)
                       + countDir(x, y, -dx, -dy, color);
        if (line >= 5) return true;
    }
    return false;
}

int GomokuBoard::checkWin() const
{
    // 全盘扫描：任一方向出现连续五子即该色胜；无胜负而棋盘填满则平局。
    for (int x = 0; x < BOARD_SIZE; ++x) {
        for (int y = 0; y < BOARD_SIZE; ++y) {
            const int color = grid_[x][y];
            if (color == Color::EMPTY) continue;

            for (int d = 0; d < 4; ++d) {
                const int dx = DIRECTIONS[d][0];
                const int dy = DIRECTIONS[d][1];
                // 从每个子向单侧数，遇链首自然跳过重复，故 1 + 单侧 >=5 即可。
                if (1 + countDir(x, y, dx, dy, color) >= 5) {
                    return color;  // 1=黑胜 / 2=白胜
                }
            }
        }
    }

    return isFull() ? 3 : 0;  // 3=平局 / 0=继续
}

void GomokuBoard::getValidMoves(std::vector<Point>& moves, int range) const
{
    if (range < 0) range = 0;

    bool visited[BOARD_SIZE][BOARD_SIZE];
    for (int i = 0; i < BOARD_SIZE; ++i) {
        for (int j = 0; j < BOARD_SIZE; ++j) {
            visited[i][j] = false;
        }
    }

    for (int i = 0; i < BOARD_SIZE; ++i) {
        for (int j = 0; j < BOARD_SIZE; ++j) {
            if (grid_[i][j] == Color::EMPTY) continue;

            for (int dx = -range; dx <= range; ++dx) {
                for (int dy = -range; dy <= range; ++dy) {
                    if (dx == 0 && dy == 0) continue;
                    const int nx = i + dx;
                    const int ny = j + dy;
                    if (!inBounds(nx, ny))          continue;
                    if (visited[nx][ny])            continue;
                    if (grid_[nx][ny] != Color::EMPTY) continue;

                    moves.push_back(Point(nx, ny));
                    visited[nx][ny] = true;
                }
            }
        }
    }

    // 空盘特例：没有任何参考子时，允许落在天元，避免 AI 首手无候选。
    if (moves.empty()) {
        const int c = BOARD_SIZE / 2;
        moves.push_back(Point(c, c));
    }
}

} // namespace AIWZQ
