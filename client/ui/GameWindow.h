#pragma once

// GameWindow — 对局窗口
// 棋盘显示、黑白信息与时间条、步计时、悔棋/和棋/认输。
// 意图通过 signals 抛出；外部 controller 通过 public 方法回填数据。
// UI 层自己维护一份 15x15 显示镜像，不依赖网络层与 common 的 .cpp 实现。

#include <QMainWindow>
#include <QString>
#include <QPoint>

#include "../../common/include/Types.h"

class GomokuBoardWidget;
class QLabel;
class QPushButton;
class QProgressBar;
class QTimer;
class QResizeEvent;

class GameWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit GameWindow(QWidget *parent = nullptr);

    // ---------- 对局初始化 ----------
    void startGame(int myColor,
                   const QString &myName,
                   const QString &opponentName,
                   int totalTimeSec,
                   int stepTimeSec);

    // ---------- 外部数据回填 ----------
    void placeStoneAt(int x, int y, int color, bool playEffect = true); // 落子 + 动画
    void removeLastStone();                       // 悔棋后移除最后一子
    void setLastMove(int x, int y);
    void updateTimes(int blackTotalLeft, int whiteTotalLeft, int stepLeft);
    void setTurn(bool myTurn);                    // 是否轮到我，控制棋盘可交互
    void clearBoard();                            // 重置棋盘显示

    // 对局结束；reason: win / draw / surrender / timeout
    void endGame(int winnerColor, const QString &reason, const QString &detail = QString());

    // 请求结果提示（例如对方拒绝悔棋）
    void showMessage(const QString &msg);

    // 对局结束查询：玩家是否获胜（winnerColor == 自己的颜色）。
    // winnerColor 为 0（和棋/无胜方）时返回 false。
    bool isPlayerWinner(int winnerColor) const;

signals:
    void placeStoneRequested(int x, int y);
    void undoRequested();
    void drawRequested();
    void surrenderRequested();
    void readyRequested(bool ready);
    void leaveGameRequested();

private slots:
    void onBoardClicked(int x, int y);
    void onTimerTick();
    void onUndoRequested();
    void onDrawRequested();
    void onSurrender();

private:
    void setupUi();
    void refreshInfoLabels();
    QString colorName(int color) const;
    void showResultOverlay(const QString &text);          // 在棋盘上显示结果覆盖层
    void resizeEvent(QResizeEvent *event) override;        // 覆盖层随棋盘尺寸变化重新居中

    GomokuBoardWidget *boardWidget_;
    QLabel *blackInfoLabel_;
    QLabel *whiteInfoLabel_;
    QProgressBar *blackTimeBar_;
    QProgressBar *whiteTimeBar_;
    QLabel *stepTimerLabel_;
    QLabel *statusLabel_;
    QLabel *resultOverlay_;              // 对局结束覆盖在棋盘上的结果标签
    QPushButton *undoBtn_;
    QPushButton *drawBtn_;
    QPushButton *surrenderBtn_;
    QTimer *gameTimer_;

    int grid_[AIWZQ::BOARD_SIZE][AIWZQ::BOARD_SIZE];
    QPoint lastMove_;

    int myColor_ = AIWZQ::Color::BLACK;
    QString myName_;
    QString opponentName_;
    bool isMyTurn_ = false;
    int totalMax_ = 180;
    int blackTotalLeft_ = 180;
    int whiteTotalLeft_ = 180;
    int stepTimeLeft_ = 30;
    bool gameOver_ = false;
};
