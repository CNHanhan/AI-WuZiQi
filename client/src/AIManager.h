#pragma once

// AIManager：在独立 QThread 中运行 common 的可移植 GomokuAI，避免阻塞 UI。
// 采用 worker QObject 方案：startGame/onPlayerMove 通过 queued 信号投递到工作线程；
// AI 计算完成后 worker 发 moveReady/gameOver，Qt 自动排队回主线程。

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QJsonObject>

#include "GomokuAI.h"
#include "GomokuBoard.h"
#include "Types.h"

class AIWorker;   // 内部工作对象（.cpp 中定义）

class AIManager : public QObject
{
    Q_OBJECT
public:
    explicit AIManager(QObject* parent = nullptr);
    ~AIManager() override;

public slots:
    // difficulty: 1=简单 2=中等 3=困难；playerColor: AIWZQ::Color::BLACK/WHITE（玩家执色）
    void startGame(int difficulty, int playerColor);
    // 玩家落子（x,y）
    void onPlayerMove(int x, int y);
    // 悔棋（撤销玩家和 AI 各一步）
    void undoLastMove();
    // 重置/停止当前对局
    void stopGame();

signals:
    void aiMoveReady(int x, int y);
    // result 同 GomokuBoard::checkWin(): 1=黑胜 2=白胜 3=平局（GameController 据此判定胜负）
    void gameOver(int result);

    // 投递到工作线程的信号
    void sigStart(int difficulty, int aiColor);
    void sigPlayerMove(int x, int y);
    void sigUndo();

private:
    QThread*  aiThread_ = nullptr;
    AIWorker* worker_ = nullptr;
    QMutex    mutex_;
    int       playerColor_ = AIWZQ::Color::BLACK;
};

// ---------------- 内部 worker（头文件内声明，cpp 实现）----------------
class AIWorker : public QObject
{
    Q_OBJECT
public:
    explicit AIWorker(QObject* parent = nullptr);
    ~AIWorker() override;

public slots:
    void doStart(int difficulty, int aiColor);
    void doPlayerMove(int x, int y);
    void doUndo();
    void doStop();

signals:
    void moveReady(int x, int y);
    void over(int result);

private:
    void maybeTriggerAi();   // 若轮到 AI 且未结束，则计算并落子
    void evaluateAndEmit();  // 检查胜负/平局并发 over

private:
    AIWZQ::GomokuAI* ai_ = nullptr;
    AIWZQ::GomokuBoard board_;
    int aiColor_ = AIWZQ::Color::WHITE;
    int playerColor_ = AIWZQ::Color::BLACK;
    bool running_ = false;
    bool aiThinking_ = false;
};
