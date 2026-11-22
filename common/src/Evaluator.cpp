// ============================================================================
// Evaluator.cpp — 棋型评估体系实现
// 对应设计文档：05-AI算法设计.md 三、棋型评估体系
//   3.1 棋型定义与评分表
//   3.2 棋型识别（从落点沿四方向延伸，统计连子与两端空位判型）
//   3.3 局面评分函数 evaluateBoard = myScore * 1.2 - oppScore
// 纯 C++11，不依赖 Qt，服务端/客户端共用。
// ============================================================================

#include "GomokuAI.h"       // Evaluator 声明
#include "GomokuBoard.h"
#include "Types.h"

#include <algorithm>        // std::max

namespace AIWZQ {

namespace {

// ---------- 05-AI算法设计.md 3.1 棋型评分表 ----------
// 连五视为必胜，用一个远大于其它棋型、但仍在 int 内的值表示 INF。
// 设计文档写 "∞（必胜）"；用有限大值便于参与加法且不溢出。
const int SCORE_FIVE      = 100000000;  // ∞（必胜）—— 与搜索 INF 同量级
const int SCORE_OPEN_FOUR = 100000;     // 活四 _OOOO_
const int SCORE_FOUR      = 10000;      // 冲四 XOOOO_ / _OOOOX
const int SCORE_OPEN_THREE= 5000;       // 活三 _OOO__ / __OOO_
const int SCORE_THREE     = 500;        // 眠三 XOOO__ / __OOOX
const int SCORE_OPEN_TWO  = 100;        // 活二 _OO__
const int SCORE_TWO       = 10;         // 眠二 XOO__

int opponentOf(int color)
{
    return (color == Color::BLACK) ? Color::WHITE : Color::BLACK;
}

// 把一条含落点的 9 长线 (中心 index=4) 按棋型表归类并返回分数。
//
// 线的构造：以 (x,y) 为中心，沿 (dx,dy) 取 -4..+4 共 9 格；越界或对方子
// 记为 X（阻挡），空位记为 _，己方记为 O。中心格按「已落己方子」参与评估。
// 采用窗口模式匹配：
//   连五   ：出现连续 >=5 个 O
//   活四   ：连续 4 个 O，两端都是 _
//   冲四   ：连续 4 个 O，仅一端是 _
//   活三   ：连续 3 个 O，两端 _，且至少一侧能再延展成活四（空位留白）
//   眠三   ：连续 3 个 O，一端 _、一端 X 或被堵
//   活二   ：连续 2 个 O，两端 _
//   眠二   ：连续 2 个 O，一端 _、一端 X
// 返回该方向该色的最高档棋型分（同方向只取最大，避免重复累加放大）。
int scoreLine(const char* line, int len)
{
    // line[i] in {'O','X','_'}
    // 找最长连续 O 段，统计其数量、两端字符。
    int best = 0;

    for (int i = 0; i < len; ++i) {
        if (line[i] != 'O') continue;

        int j = i;
        while (j < len && line[j] == 'O') ++j;
        const int run = j - i;

        const char left  = (i - 1 >= 0)  ? line[i - 1] : 'X';  // 越界视为堵
        const char right = (j < len)     ? line[j]     : 'X';  // 越界视为堵
        const bool lOpen = (left  == '_');
        const bool rOpen = (right == '_');

        int score = 0;
        if (run >= 5) {
            score = SCORE_FIVE;
        } else if (run == 4) {
            if (lOpen && rOpen) score = SCORE_OPEN_FOUR;
            else if (lOpen || rOpen) score = SCORE_FOUR;
            else score = 0;  // 两端堵死，无价值
        } else if (run == 3) {
            if (lOpen && rOpen) {
                // 活三：两端空。若一侧再空一格（可冲成四/活四）仍按活三，
                // 否则退为眠三。窗口内 l/r 已空即视为有发展空间。
                score = SCORE_OPEN_THREE;
            } else if (lOpen || rOpen) {
                score = SCORE_THREE;
            } else {
                score = 0;
            }
        } else if (run == 2) {
            if (lOpen && rOpen) score = SCORE_OPEN_TWO;
            else if (lOpen || rOpen) score = SCORE_TWO;
            else score = 0;
        } else {
            score = 0;  // 单子不成型
        }

        if (score > best) best = score;
        i = j - 1;  // 跳过本段
    }

    return best;
}

// 生成以 (x,y) 为中心、沿 (dx,dy) 的 9 长线，color 为己方，
// 中心格按「假设 color 已落在此」处理（用于 pointScore：模拟落子后的增益）。
// centerOccupied=false 时，中心按当前实际棋盘（用于 evaluateBoard 遍历既有子）。
void buildLine(const GomokuBoard& board, int x, int y, int dx, int dy,
               int color, bool centerOccupied, char* out, int len)
{
    const int half = len / 2;  // 4
    for (int k = 0; k < len; ++k) {
        const int off = k - half;
        const int cx = x + dx * off;
        const int cy = y + dy * off;

        if (off == 0 && centerOccupied) {
            out[k] = 'O';
            continue;
        }

        const int v = board.get(cx, cy);  // 越界返回 EMPTY，需按越界处理
        const bool outside = (cx < 0 || cx >= BOARD_SIZE || cy < 0 || cy >= BOARD_SIZE);
        if (outside)            out[k] = 'X';
        else if (v == Color::EMPTY) out[k] = '_';
        else if (v == color)        out[k] = 'O';
        else                        out[k] = 'X';  // 对方子 = 阻挡
    }
}

// 沿四方向评估 (x,y) 对 color 的价值总和（每方向取最高棋型分）。
int directionalScore(const GomokuBoard& board, int x, int y, int color,
                     bool centerOccupied)
{
    int total = 0;
    for (int d = 0; d < 4; ++d) {
        const int dx = DIRECTIONS[d][0];
        const int dy = DIRECTIONS[d][1];
        char line[9];
        buildLine(board, x, y, dx, dy, color, centerOccupied, line, 9);
        total += scoreLine(line, 9);
    }
    return total;
}

} // anonymous namespace

// ---------- 3.3 局面评分：遍历每个棋子，攻守加权 ----------
int Evaluator::evaluateBoard(const GomokuBoard& board, int aiColor)
{
    int myScore = 0;
    int oppScore = 0;
    const int oppColor = opponentOf(aiColor);

    for (int i = 0; i < BOARD_SIZE; ++i) {
        for (int j = 0; j < BOARD_SIZE; ++j) {
            const int c = board.get(i, j);
            if (c == aiColor) {
                // 已落子：中心按实际子参与（centerOccupied 效果等同于读到 O）
                myScore += directionalScore(board, i, j, aiColor, false);
            } else if (c == oppColor) {
                oppScore += directionalScore(board, i, j, oppColor, false);
            }
        }
    }

    // 文档 3.3：进攻权重 > 防守权重，返回 myScore * 1.2f - oppScore。
    // 用整数 12/10 复现 1.2 倍，避免浮点并保证确定性。
    return myScore * 12 / 10 - oppScore;
}

// ---------- 落子后该点的进攻/防守增益 ----------
// 文档 GomokuAI.h 注释：「落子后该点的进攻/防守增益」。
// 以「该点已落 color 子」模拟，返回四方向棋型价值；供走法排序使用。
int Evaluator::pointScore(const GomokuBoard& board, int x, int y, int color)
{
    // 非空格不可落子，返回 0（走法排序时调用方已先筛掉非空格）。
    if (board.get(x, y) != Color::EMPTY) return 0;

    // 文档：从该点沿 4 方向延伸统计连子与两端空位判型。
    // 以「该点已落 color 子」（centerOccupied=true）模拟落子后的四方向棋型，
    // 返回该点的落子价值（走法排序据此把高价值点排到前面）。
    return directionalScore(board, x, y, color, true);
}

} // namespace AIWZQ
