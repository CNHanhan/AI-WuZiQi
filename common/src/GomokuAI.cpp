// ============================================================================
// GomokuAI.cpp — 离线 AI 引擎 PIMPL 实现 + 三档难度分派
// 对应设计文档：05-AI算法设计.md
//   4.1 简单AI：findWinningMove(己方必胜) → 堵对手必胜 → 随机候选点
//   4.2 中等AI：Minimax + Alpha-Beta 剪枝（原样伪代码）
//   5.2 迭代深化（depth=2,4,6，超时保留上一层最优）
//   5.1 Zobrist 置换表缓存已评估局面
//   4.3/5.3 困难AI：MCTS 多线程（委托 AIInternal::runMCTS，见 MCTS.cpp）
// 纯 C++11，不依赖 Qt，服务端/客户端共用。
// ============================================================================

#include "GomokuAI.h"       // common/include
#include "GomokuBoard.h"
#include "Types.h"
#include "Zobrist.h"        // common/src 内部头：Zobrist + TranspositionTable

#include <vector>
#include <cstdlib>          // std::rand / std::srand
#include <ctime>            // std::time
#include <thread>           // std::thread::hardware_concurrency
#include <chrono>

namespace AIWZQ {

// MCTS.cpp 提供的困难档入口（前向声明；实现见 common/src/MCTS.cpp）。
namespace AIInternal {
Point runMCTS(const GomokuBoard& board, int aiColor,
              int iterations, int numThreads, int timeLimitMs);
}

// ---------- 搜索常量（文档 4.2 / 6 难度参数表） ----------
const int INF = 1000000000;             // 文档 4.2：负无穷~正无穷
const int MEDIUM_MAX_DEPTH = 6;         // 文档 6：中等 4~6 层；4.2 DEPTH=6
const int HARD_ITERATIONS = 30000;      // 文档 6：困难 10000~50000 次模拟（取中值）
const int HARD_MIN_THREADS = 2;         // 文档 5.3：多线程并行

// ============================================================================
// PIMPL：Impl 内部类
// ============================================================================
struct GomokuAI::Impl {
    int timeLimitMs_;                                   // 思考时限（ms），0=不限
    AIInternal::Zobrist zobrist_;                       // 5.1 Zobrist 哈希器
    AIInternal::TranspositionTable tt_;                 // 5.1 置换表
    std::chrono::steady_clock::time_point startTime_;   // 本轮搜索起始时间
    bool timedOut_;                                     // 本轮是否已超时
    long long nodesCounter_;                            // 节点计数（降低时钟检查频率）

    Impl() : timeLimitMs_(0), timedOut_(false), nodesCounter_(0)
    {
        std::srand(static_cast<unsigned int>(std::time(NULL)));
    }

    // 是否已超时（文档 5.2 isTimeout）。
    bool isTimeout() const
    {
        if (timeLimitMs_ <= 0) return false;
        const long long elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - startTime_).count();
        return elapsed >= timeLimitMs_;
    }

    // 对方颜色。
    static int opponentOf(int color)
    {
        return (color == Color::BLACK) ? Color::WHITE : Color::BLACK;
    }

    // ---------- 4.1 findWinningMove：找 color 的必胜落子点 ----------
    Point findWinningMove(const GomokuBoard& board, int color) const
    {
        std::vector<Point> moves;
        board.getValidMoves(moves, 1);   // 成五点必在已有子邻近
        for (std::size_t i = 0; i < moves.size(); ++i) {
            GomokuBoard temp = board;
            if (temp.placeStone(moves[i].x, moves[i].y, color) &&
                temp.isWinAt(moves[i].x, moves[i].y, color)) {
                return moves[i];
            }
        }
        return Point(-1, -1);
    }

    // ---------- 4.2 sortMoves：走法排序（启发式优化） ----------
    void sortMoves(std::vector<Point>& moves, const GomokuBoard& board, int color) const
    {
        // 按落子价值降序插入排序（候选规模小，稳定、确定）。
        std::vector<int> scores(moves.size());
        for (std::size_t i = 0; i < moves.size(); ++i) {
            scores[i] = Evaluator::pointScore(board, moves[i].x, moves[i].y, color);
        }
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

    // ---------- 4.2 alphaBeta：严格按文档伪代码 ----------
    int alphaBeta(GomokuBoard& board, int depth, int alpha, int beta,
                  int currentColor, int aiColor)
    {
        // 超时协作：每 256 节点查一次时钟，超时后快速收敛返回评估。
        if ((++nodesCounter_ & 0xFF) == 0 && isTimeout()) timedOut_ = true;
        if (timedOut_) return Evaluator::evaluateBoard(board, aiColor);

        // 终止条件（文档 4.2）。
        const int winner = board.checkWin();
        if (winner == aiColor)             return INF;
        if (winner == opponentOf(aiColor)) return -INF;
        if (depth == 0) {
            // 5.1：叶节点评估值入置换表（评估只依赖局面，缓存安全）。
            const uint64_t hash = zobrist_.getBoardHash(board);
            int cached = 0;
            if (tt_.get(hash, cached)) return cached;
            const int score = Evaluator::evaluateBoard(board, aiColor);
            tt_.put(hash, score);
            return score;
        }

        std::vector<Point> moves;
        board.getValidMoves(moves, 2);              // 搜索范围：已有棋子周围2格
        sortMoves(moves, board, currentColor);      // 走法排序

        for (std::size_t i = 0; i < moves.size(); ++i) {
            const Point& mv = moves[i];
            board.placeStone(mv.x, mv.y, currentColor);
            const int score = alphaBeta(board, depth - 1, alpha, beta,
                                        opponentOf(currentColor), aiColor);
            board.removeStone(mv.x, mv.y);          // 回溯（成对 place/remove）

            if (currentColor == aiColor) {
                if (score > alpha) alpha = score;
                if (alpha >= beta) break;           // Beta 剪枝
            } else {
                if (score < beta) beta = score;
                if (alpha >= beta) break;           // Alpha 剪枝
            }
        }
        return (currentColor == aiColor) ? alpha : beta;
    }

    // ---------- 4.2 MediumAI::getMove 核心：固定深度根搜索 ----------
    Point alphaBetaSearch(const GomokuBoard& board, int aiColor, int depth)
    {
        Point bestMove(-1, -1);
        int bestScore = -INF;

        std::vector<Point> moves;
        board.getValidMoves(moves, 2);
        sortMoves(moves, board, aiColor);

        for (std::size_t i = 0; i < moves.size(); ++i) {
            if (timedOut_) break;
            const Point& mv = moves[i];
            GomokuBoard temp = board;               // 拷贝棋盘
            temp.placeStone(mv.x, mv.y, aiColor);
            const int score = alphaBeta(temp, depth - 1, -INF, INF,
                                        opponentOf(aiColor), aiColor);
            if (score > bestScore) {
                bestScore = score;
                bestMove = mv;
            }
        }
        return bestMove;
    }

    // ---------- 5.2 迭代深化：depth=2,4,6 ----------
    Point iterativeDeepening(const GomokuBoard& board, int aiColor, int maxDepth)
    {
        startTime_ = std::chrono::steady_clock::now();
        timedOut_ = false;
        nodesCounter_ = 0;
        tt_.clear();

        Point bestMove(-1, -1);
        for (int depth = 2; depth <= maxDepth; depth += 2) {
            const Point mv = alphaBetaSearch(board, aiColor, depth);
            if (!isTimeout() && mv.valid()) {
                bestMove = mv;                      // 未超时才接受本层结果
            } else {
                break;                              // 超时：保留上一层最优解
            }
        }

        if (!bestMove.valid()) {                    // 兜底：首个合法点
            std::vector<Point> moves;
            board.getValidMoves(moves, 1);
            if (!moves.empty()) bestMove = moves[0];
        }
        return bestMove;
    }

    // ---------- 三档分派 ----------
    Point getMove(const GomokuBoard& board, int aiColor, int difficulty)
    {
        if (difficulty == 1) {
            // 4.1 简单：己方必胜 → 堵对手必胜 → 随机候选
            const Point win = findWinningMove(board, aiColor);
            if (win.valid()) return win;
            const Point block = findWinningMove(board, opponentOf(aiColor));
            if (block.valid()) return block;

            std::vector<Point> moves;
            board.getValidMoves(moves, 1);
            if (moves.empty()) return Point(-1, -1);
            return moves[std::rand() % moves.size()];
        }

        if (difficulty == 2) {
            // 4.2 + 5.2 中等：Alpha-Beta + 迭代深化 + 置换表
            return iterativeDeepening(board, aiColor, MEDIUM_MAX_DEPTH);
        }

        // difficulty == 3：困难：MCTS 多线程（4.3 / 5.3）
        int threads = static_cast<int>(std::thread::hardware_concurrency());
        if (threads < HARD_MIN_THREADS) threads = HARD_MIN_THREADS;
        if (threads > 8) threads = 8;
        return AIInternal::runMCTS(board, aiColor, HARD_ITERATIONS, threads, timeLimitMs_);
    }
};

// ============================================================================
// GomokuAI 公有接口（转发到 PIMPL）
// ============================================================================
GomokuAI::GomokuAI(int difficulty)
    : impl_(new Impl()), difficulty_(difficulty)
{
}

GomokuAI::~GomokuAI()
{
    delete impl_;
    impl_ = NULL;
}

Point GomokuAI::getMove(const GomokuBoard& board, int aiColor)
{
    if (aiColor != Color::BLACK && aiColor != Color::WHITE) return Point(-1, -1);
    return impl_->getMove(board, aiColor, difficulty_);
}

void GomokuAI::setTimeLimitMs(int ms)
{
    impl_->timeLimitMs_ = (ms < 0) ? 0 : ms;
}

} // namespace AIWZQ
