#pragma once

// RoomWindow — 房间等待窗口
// 展示双方玩家信息、准备状态、开始倒计时；意图通过 signals 抛出。

#include <QMainWindow>
#include <QString>

class QLabel;
class QPushButton;
class QTimer;
class QCheckBox;

class RoomWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit RoomWindow(QWidget *parent = nullptr);

    // ---------- 数据回填 ----------
    void setRoomId(qint64 roomId);
    void setSelfPlayer(const QString &name, int elo);
    void setOpponentPlayer(const QString &name, int elo);
    void setSelfReady(bool ready);
    void setOpponentReady(bool ready);
    void clearOpponent();

    // 开始倒计时：seconds <= 0 表示取消倒计时
    void startCountdown(int seconds);
    void cancelCountdown();

    // 等待对手超时（2.5 分钟）：进入房间/重新等待对手时启动，对手到来或对局开始时停止
    void startWaitingTimeout();
    void stopWaitingTimeout();
    bool hasOpponent() const { return opponentPresent_; }

signals:
    void readyRequested(bool ready);
    void leaveRoomRequested();
    void waitTimedOut();   // 等待对手超过 150 秒，请求 controller 提示并返回大厅

private slots:
    void onReadyToggled(bool checked);
    void onLeaveClicked();
    void onCountdownTick();
    void onWaitTimeout();

private:
    void setupUi();
    void updateStatus();

    QLabel    *roomIdLabel_;
    QLabel    *selfInfoLabel_;
    QLabel    *opponentInfoLabel_;
    QLabel    *selfReadyLabel_;
    QLabel    *opponentReadyLabel_;
    QLabel    *countdownLabel_;
    QCheckBox *readyCheck_;
    QPushButton *leaveBtn_;
    QTimer    *countdownTimer_;
    QTimer    *waitTimer_;

    qint64 roomId_ = 0;
    int    remainingSeconds_ = 0;
    bool   selfReady_ = false;
    bool   opponentReady_ = false;
    bool   opponentPresent_ = false;
};
