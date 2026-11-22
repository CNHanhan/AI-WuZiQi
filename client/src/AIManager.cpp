#include "AIManager.h"

#include <QDebug>

// ===================== AIManager =====================
AIManager::AIManager(QObject* parent)
    : QObject(parent)
{
    aiThread_ = new QThread(this);
    worker_ = new AIWorker();   // 无 parent，交由线程负责 deleteLater
    worker_->moveToThread(aiThread_);

    // 主线程 -> 工作线程
    connect(this, &AIManager::sigStart,      worker_, &AIWorker::doStart);
    connect(this, &AIManager::sigPlayerMove, worker_, &AIWorker::doPlayerMove);
    connect(this, &AIManager::sigUndo,       worker_, &AIWorker::doUndo);

    // 工作线程 -> 主线程（Qt::AutoConnection 自动排队）
    connect(worker_, &AIWorker::moveReady, this, &AIManager::aiMoveReady);
    connect(worker_, &AIWorker::over,      this, &AIManager::gameOver);

    // 线程结束时清理 worker
    connect(aiThread_, &QThread::finished, worker_, &QObject::deleteLater);
    aiThread_->start();
}

AIManager::~AIManager()
{
    if (aiThread_ && aiThread_->isRunning()) {
        aiThread_->quit();
        aiThread_->wait(3000);
    }
}

void AIManager::startGame(int difficulty, int playerColor)
{
    QMutexLocker locker(&mutex_);
    playerColor_ = playerColor;
    const int aiColor = (playerColor == AIWZQ::Color::BLACK)
                        ? AIWZQ::Color::WHITE : AIWZQ::Color::BLACK;
    emit sigStart(difficulty, aiColor);
}

void AIManager::onPlayerMove(int x, int y)
{
    emit sigPlayerMove(x, y);
}

void AIManager::undoLastMove()
{
    emit sigUndo();
}

void AIManager::stopGame()
{
    // worker 端通过 sigStart 重置或直接停；这里用 doStop 直连
    QMetaObject::invokeMethod(worker_, "doStop", Qt::QueuedConnection);
}

// ===================== AIWorker =====================
AIWorker::AIWorker(QObject* parent)
    : QObject(parent)
{
    ai_ = new AIWZQ::GomokuAI(2);
}

AIWorker::~AIWorker()
{
    delete ai_;
    ai_ = nullptr;
}

void AIWorker::doStart(int difficulty, int aiColor)
{
    delete ai_;
    ai_ = new AIWZQ::GomokuAI(difficulty);
    ai_->setTimeLimitMs(1000);   // 单步最多 1s，避免卡线程太久
    aiColor_ = aiColor;
    playerColor_ = (aiColor == AIWZQ::Color::BLACK)
                   ? AIWZQ::Color::WHITE : AIWZQ::Color::BLACK;
    board_.clear();
    running_ = true;
    aiThinking_ = false;
    // 黑先手：若 AI 执黑，则 AI 先落子
    maybeTriggerAi();
}

void AIWorker::doPlayerMove(int x, int y)
{
    if (!running_) return;
    if (!board_.placeStone(x, y, playerColor_)) {
        qWarning() << "[AIWorker] invalid player move" << x << y;
        return;
    }
    evaluateAndEmit();
    if (running_) maybeTriggerAi();
}

void AIWorker::doUndo()
{
    if (!running_) return;
    // 撤销 AI 与玩家最近各一步（顺序：玩家落 -> AI 落，history 末尾是 AI）
    if (!board_.history().empty()) board_.undo();
    if (!board_.history().empty()) board_.undo();
}

void AIWorker::doStop()
{
    running_ = false;
    aiThinking_ = false;
}

void AIWorker::maybeTriggerAi()
{
    if (!running_ || aiThinking_) return;
    if (board_.checkWin() != 0 || board_.isFull()) return;
    // 判断当前是否轮到 AI：黑棋步数 == 白棋步数 -> 黑走
    int black = 0, white = 0;
    for (int i = 0; i < AIWZQ::BOARD_SIZE; ++i)
        for (int j = 0; j < AIWZQ::BOARD_SIZE; ++j) {
            const int c = board_.get(i, j);
            if (c == AIWZQ::Color::BLACK) ++black;
            else if (c == AIWZQ::Color::WHITE) ++white;
        }
    const int turn = (black == white) ? AIWZQ::Color::BLACK : AIWZQ::Color::WHITE;
    if (turn != aiColor_) return;

    aiThinking_ = true;
    const AIWZQ::Point p = ai_->getMove(board_, aiColor_);
    aiThinking_ = false;
    if (!p.valid()) {
        emit over(3);   // 平局
        running_ = false;
        return;
    }
    board_.placeStone(p.x, p.y, aiColor_);
    emit moveReady(p.x, p.y);
    evaluateAndEmit();
}

void AIWorker::evaluateAndEmit()
{
    const int w = board_.checkWin();
    if (w == 1 || w == 2 || w == 3) {   // checkWin(): 1=黑胜 2=白胜 3=平局
        emit over(w);
        running_ = false;
    } else if (board_.isFull()) {
        emit over(3);
        running_ = false;
    }
}
