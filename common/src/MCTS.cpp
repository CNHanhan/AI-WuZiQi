// ============================================================================
// MCTS.cpp — 困难档蒙特卡洛树搜索实现
// 对应设计文档：05-AI算法设计.md 4.3 MCTS（蒙特卡洛树搜索）
//   4.3 MCTSNode 结构（board/color/parent/children/visits/wins/expanded）
//   4.3 四阶段 select / expand / simulate / backpropagate
//   4.3 UCB1 公式（c=1.414，未访问节点返回 1e9）、bestChild
//   5.3 多线程搜索（std::thread 并行跑 MCTS 模拟）
// 纯 C++11，不依赖 Qt；提供 AIInternal::runMCTS 供 GomokuAI.cpp 调用。
// ============================================================================

#include "GomokuBoard.h"
#include "GomokuAI.h"       // Evaluator（候选走法启发式排序）
#include "Types.h"

#include <memory>           // std::unique_ptr
#include <vector>
#include <thread>           // std::thread
#include <random>           // std::mt19937 / std::uniform_int_distribution
#include <cmath>            // std::sqrt / std::log
#include <algorithm>        // std::sort（备用）
#include <chrono>
#include <atomic>           // std::atomic<bool> 线程停止信号

namespace AIWZQ {
namespace AIInternal {

// ---------- 文档 4.3 MCTSNode ----------
struct MCTSNode {
    GomokuBoard board;                                // 当前局面（按值拷贝）
    int color;                                        // 当前要落子的颜色
    MCTSNode* parent;                                 // 父节点
    std::vector<std::unique_ptr<MCTSNode> > children; // 子节点
    int visits;                                       // 访问次数
    float wins;                                       // 累计胜率（对 AI 而言）
    bool expanded;                                    // 是否已展开
    Point move;                                       // 到达本节点所落的子（根节点 -1,-1）

    MCTSNode(const GomokuBoard& b, int c, MCTSNode* p, const Point& m)
        : board(b), color(c), parent(p), visits(0), wins(0.0f),
          expanded(false), move(m) {}
};

// 线程安全随机源：std::rand 非线程安全，改用 C++11 thread_local mt19937。
static std::mt19937& threadRng()
{
    static thread_local std::mt19937 rng(
        static_cast<unsigned int>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()
            ^ static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(&rng))));
    return rng;
}

static int opponentOf(int color)
{
    return (color == Color::BLACK) ? Color::WHITE : Color::BLACK;
}

// ---------- 文档 4.3 UCB1 公式 ----------
// 未访问节点返回 1e9（优先访问）；否则 wins/visits + c*sqrt(log(parent.visits)/visits)。
static double ucb1(const MCTSNode* child, const MCTSNode* parent, double c = 1.414)
{
    if (child->visits == 0) return 1e9;
    if (parent->visits == 0) return 1e9;
    return (static_cast<double>(child->wins) / child->visits)
         + c * std::sqrt(std::log(static_cast<double>(parent->visits)) / child->visits);
}

// ---------- 文档 4.3 bestChild ----------
static MCTSNode* bestChild(MCTSNode* node, double c = 1.414)
{
    MCTSNode* best = NULL;
    double bestUcb = -1.0;
    for (std::size_t i = 0; i < node->children.size(); ++i) {
        MCTSNode* child = node->children[i].get();
        const double u = ucb1(child, node, c);
        if (u > bestUcb) {
            bestUcb = u;
            best = child;
        }
    }
    return best;
}

// ---------- 文档 4.3 select：选择阶段 ----------
// 从 root 向下：节点「已展开且有子节点」就用 UCB1 选最优子节点下降；
// 返回第一个「尚未展开」的节点（可能是 root 本身）。
static MCTSNode* select(MCTSNode* root)
{
    MCTSNode* node = root;
    while (node->expanded && !node->children.empty()) {
        MCTSNode* next = bestChild(node);
        if (next == NULL) break;
        node = next;
    }
    return node;
}

// ---------- 文档 4.3 expand：展开阶段 ----------
// 候选点来自 getValidMoves，并用 pointScore 做启发式排序（高分在前），
// 与文档 4.2 sortMoves 的启发式优化思想一致。
static void expand(MCTSNode* node)
{
    if (node->expanded) return;
    if (node->board.checkWin() != 0) {
        node->expanded = true;   // 终局节点无需展开
        return;
    }

    std::vector<Point> moves;
    node->board.getValidMoves(moves, 1);

    if (!moves.empty()) {
        std::vector<int> scores(moves.size());
        for (std::size_t i = 0; i < moves.size(); ++i) {
            scores[i] = Evaluator::pointScore(node->board, moves[i].x, moves[i].y, node->color);
        }
        // 插入排序（候选规模小且稳定），高分在前。
        for (std::size_t i = 1; i < moves.size(); ++i) {
            const Point pm = moves[i];
            const int ps = scores[i];
            std::size_t j = i;
            while (j > 0 && scores[j - 1] < ps) {
                moves[j] = moves[j - 1];
                scores[j] = scores[j - 1];
                --j;
            }
            moves[j] = pm;
            scores[j] = ps;
        }
    }

    for (std::size_t i = 0; i < moves.size(); ++i) {
        GomokuBoard childBoard = node->board;
        if (!childBoard.placeStone(moves[i].x, moves[i].y, node->color)) continue;
        MCTSNode* child = new MCTSNode(childBoard, opponentOf(node->color), node, moves[i]);
        node->children.push_back(std::unique_ptr<MCTSNode>(child));
    }
    node->expanded = true;
}

// ---------- 文档 4.3 simulate：模拟阶段 ----------
// 从节点局面随机对弈至终局，返回 AI 视角结果：胜=1.0 / 负=0.0 / 平=0.5。
// 落子后用棋盘接口 isWinAt（文档：供增量判定）判五，避免每步全盘 checkWin 扫描。
static float simulate(MCTSNode* node, int aiColor)
{
    GomokuBoard board = node->board;   // 拷贝，随机推演不改变原节点
    int curColor = node->color;

    // 起点局面本身可能已终局（例如展开到成五的节点）。
    const int startWinner = board.checkWin();
    if (startWinner == aiColor)             return 1.0f;
    if (startWinner == opponentOf(aiColor)) return 0.0f;
    if (startWinner == 3)                   return 0.5f;

    const int maxPlies = BOARD_SIZE * BOARD_SIZE;  // 步数上限，防异常死循环
    for (int ply = 0; ply < maxPlies; ++ply) {
        std::vector<Point> moves;
        board.getValidMoves(moves, 1);
        if (moves.empty()) return 0.5f;   // 无合法点 = 平局

        std::uniform_int_distribution<int> dist(0, static_cast<int>(moves.size()) - 1);
        const Point& mv = moves[dist(threadRng())];
        board.placeStone(mv.x, mv.y, curColor);

        // 增量判定最后一手是否成五（GomokuBoard.h: isWinAt 供增量判定）。
        if (board.isWinAt(mv.x, mv.y, curColor)) {
            return (curColor == aiColor) ? 1.0f : 0.0f;
        }
        if (board.isFull()) return 0.5f;   // 落满棋盘 = 平局

        curColor = opponentOf(curColor);
    }
    return 0.5f;
}

// ---------- 文档 4.3 backpropagate：回溯阶段 ----------
static void backpropagate(MCTSNode* node, float result)
{
    for (MCTSNode* p = node; p != NULL; p = p->parent) {
        p->visits += 1;
        p->wins   += result;   // 统一按 AI 视角累计
    }
}

// ---------- 单次完整迭代：select → expand → simulate → backpropagate ----------
static void mctsIteration(MCTSNode* root, int aiColor)
{
    MCTSNode* node = select(root);              // 1. 选择
    expand(node);                               // 2. 展开

    MCTSNode* leaf = node;
    if (!node->children.empty()) {
        MCTSNode* chosen = bestChild(node);     // 未访问子节点 UCB1=1e9，优先被选中
        if (chosen != NULL) leaf = chosen;
    }

    const float result = simulate(leaf, aiColor); // 3. 模拟
    backpropagate(leaf, result);                   // 4. 回溯
}

// ---------- 单线程结果汇总 ----------
struct MctsResult {
    std::vector<Point> moves;
    std::vector<int>   visits;
    std::vector<float> wins;
};

// ---------- 单个线程的 MCTS 搜索（独立树，避免共享状态数据竞争） ----------
// 计时下沉到线程内部：超时即置全局 stop 信号并退出，主线程 join 自然收束。
static MctsResult mctsOneThread(const GomokuBoard& board, int aiColor,
                                int iterations, int timeLimitMs,
                                std::atomic<bool>& stopFlag)
{
    MctsResult result;
    MCTSNode root(board, aiColor, NULL, Point(-1, -1));

    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    int checkCounter = 0;

    for (int i = 0; i < iterations; ++i) {
        if (stopFlag.load()) break;
        mctsIteration(&root, aiColor);

        // 每 64 次迭代检查一次时间（降低时钟开销）；超时置停止信号。
        if (timeLimitMs > 0 && (++checkCounter % 64 == 0)) {
            const long long elapsedMs =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();
            if (elapsedMs >= timeLimitMs) {
                stopFlag.store(true);
                break;
            }
        }
    }

    for (std::size_t k = 0; k < root.children.size(); ++k) {
        const MCTSNode* child = root.children[k].get();
        result.moves.push_back(child->move);
        result.visits.push_back(child->visits);
        result.wins.push_back(child->wins);
    }
    return result;   // root.children 由 root 的 unique_ptr 自动释放
}

// ============================================================================
// 对外入口：供 GomokuAI.cpp 困难档调用。
// iterations   总模拟次数（10000~50000）
// numThreads   并行线程数（文档 5.3）
// timeLimitMs  >0 时按时间截断，超时保留当前统计的最优解
// ============================================================================
Point runMCTS(const GomokuBoard& board, int aiColor,
              int iterations, int numThreads, int timeLimitMs)
{
    if (numThreads < 1) numThreads = 1;
    if (iterations < 1) iterations = 1;

    const int perThread = iterations / numThreads;
    const int remainder = iterations % numThreads;

    std::atomic<bool> stopFlag(false);
    std::vector<MctsResult> partial(numThreads);
    std::vector<std::thread> threads;
    threads.reserve(numThreads);

    for (int t = 0; t < numThreads; ++t) {
        const int iters = perThread + (t < remainder ? 1 : 0);
        threads.push_back(std::thread([&, t, iters]() {
            partial[t] = mctsOneThread(board, aiColor, iters, timeLimitMs, stopFlag);
        }));
    }

    // 主线程直接 join：各线程或跑满份额、或超时自行退出，无死等。
    for (std::size_t i = 0; i < threads.size(); ++i) {
        if (threads[i].joinable()) threads[i].join();
    }

    // 合并所有线程的根层统计（同一走法累加）。
    std::vector<Point> allMoves;
    std::vector<int>   allVisits;
    std::vector<float> allWins;
    for (std::size_t t = 0; t < partial.size(); ++t) {
        for (std::size_t k = 0; k < partial[t].moves.size(); ++k) {
            const Point& mv = partial[t].moves[k];
            std::size_t found = allMoves.size();
            for (std::size_t m = 0; m < allMoves.size(); ++m) {
                if (allMoves[m] == mv) { found = m; break; }
            }
            if (found == allMoves.size()) {
                allMoves.push_back(mv);
                allVisits.push_back(partial[t].visits[k]);
                allWins.push_back(partial[t].wins[k]);
            } else {
                allVisits[found] += partial[t].visits[k];
                allWins[found]   += partial[t].wins[k];
            }
        }
    }

    if (allMoves.empty()) {
        std::vector<Point> moves;
        board.getValidMoves(moves, 1);
        return moves.empty() ? Point(-1, -1) : moves[0];
    }

    // 选择访问次数最多者（MCTS 标准：root 子节点 max visits）。
    std::size_t bestIdx = 0;
    for (std::size_t m = 1; m < allMoves.size(); ++m) {
        if (allVisits[m] > allVisits[bestIdx]) bestIdx = m;
    }
    return allMoves[bestIdx];
}

} // namespace AIInternal
} // namespace AIWZQ
