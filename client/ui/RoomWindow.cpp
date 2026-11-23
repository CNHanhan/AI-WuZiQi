#include "RoomWindow.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QTimer>
#include <QFont>
#include <QSignalBlocker>

RoomWindow::RoomWindow(QWidget *parent)
    : QMainWindow(parent)
    , countdownTimer_(new QTimer(this))
    , waitTimer_(new QTimer(this))
{
    setupUi();
    countdownTimer_->setInterval(1000);
    connect(countdownTimer_, &QTimer::timeout, this, &RoomWindow::onCountdownTick);

    // 等待对手的 150 秒超时定时器：单次触发，独立于对局开始倒计时
    waitTimer_->setSingleShot(true);
    waitTimer_->setInterval(150 * 1000);
    connect(waitTimer_, &QTimer::timeout, this, &RoomWindow::onWaitTimeout);
}

void RoomWindow::setupUi()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    roomIdLabel_ = new QLabel(QStringLiteral("房间：--"), central);
    selfInfoLabel_ = new QLabel(QStringLiteral("等待玩家..."), central);
    opponentInfoLabel_ = new QLabel(QStringLiteral("等待对手加入..."), central);
    selfReadyLabel_ = new QLabel(QStringLiteral("未准备"), central);
    opponentReadyLabel_ = new QLabel(QStringLiteral("未准备"), central);
    countdownLabel_ = new QLabel(QStringLiteral(""), central);
    QFont big = countdownLabel_->font();
    big.setPointSize(28);
    big.setBold(true);
    countdownLabel_->setFont(big);
    countdownLabel_->setAlignment(Qt::AlignCenter);

    readyCheck_ = new QCheckBox(QStringLiteral("准备"), central);
    leaveBtn_ = new QPushButton(QStringLiteral("离开房间"), central);

    QGroupBox *selfBox = new QGroupBox(QStringLiteral("我"), central);
    QVBoxLayout *selfLayout = new QVBoxLayout(selfBox);
    selfLayout->addWidget(selfInfoLabel_);
    selfLayout->addWidget(selfReadyLabel_);

    QGroupBox *oppBox = new QGroupBox(QStringLiteral("对手"), central);
    QVBoxLayout *oppLayout = new QVBoxLayout(oppBox);
    oppLayout->addWidget(opponentInfoLabel_);
    oppLayout->addWidget(opponentReadyLabel_);

    QHBoxLayout *players = new QHBoxLayout;
    players->addWidget(selfBox);
    players->addWidget(oppBox);

    QHBoxLayout *controls = new QHBoxLayout;
    controls->addWidget(readyCheck_);
    controls->addStretch(1);
    controls->addWidget(leaveBtn_);

    QVBoxLayout *main = new QVBoxLayout(central);
    main->addWidget(roomIdLabel_);
    main->addLayout(players);
    main->addWidget(countdownLabel_);
    main->addLayout(controls);

    setWindowTitle(QStringLiteral("房间 - AI-WuZiQi"));
    resize(480, 360);

    connect(readyCheck_, &QCheckBox::toggled, this, &RoomWindow::onReadyToggled);
    connect(leaveBtn_, &QPushButton::clicked, this, &RoomWindow::onLeaveClicked);
}

// ---------- 数据回填 ----------
void RoomWindow::setRoomId(qint64 roomId)
{
    roomId_ = roomId;
    roomIdLabel_->setText(QStringLiteral("房间 ID：%1").arg(roomId));
}

void RoomWindow::setSelfPlayer(const QString &name, int elo)
{
    selfInfoLabel_->setText(QStringLiteral("%1  [ELO %2]").arg(name).arg(elo));
}

void RoomWindow::setOpponentPlayer(const QString &name, int elo)
{
    opponentInfoLabel_->setText(QStringLiteral("%1  [ELO %2]").arg(name).arg(elo));
    opponentPresent_ = true;
    // 对手已加入，取消“等待对手”的 150 秒超时
    stopWaitingTimeout();
}

void RoomWindow::setSelfReady(bool ready)
{
    selfReady_ = ready;
    if (readyCheck_->isChecked() != ready) {
        QSignalBlocker blocker(readyCheck_);
        readyCheck_->setChecked(ready);
    }
    updateStatus();
}

void RoomWindow::setOpponentReady(bool ready)
{
    opponentReady_ = ready;
    updateStatus();
}

void RoomWindow::clearOpponent()
{
    opponentInfoLabel_->setText(QStringLiteral("等待对手加入..."));
    opponentPresent_ = false;
    setOpponentReady(false);
    cancelCountdown();
    // 没有对手 = 仍在等待，开始 150 秒等待超时计时
    startWaitingTimeout();
}

void RoomWindow::startCountdown(int seconds)
{
    remainingSeconds_ = seconds;
    if (remainingSeconds_ <= 0) {
        cancelCountdown();
        return;
    }
    countdownLabel_->setText(QStringLiteral("对局开始倒计时：%1 秒").arg(remainingSeconds_));
    if (!countdownTimer_->isActive())
        countdownTimer_->start();
}

void RoomWindow::cancelCountdown()
{
    countdownTimer_->stop();
    remainingSeconds_ = 0;
    countdownLabel_->clear();
}

void RoomWindow::startWaitingTimeout()
{
    // 仅在确实没有对手时才计时，避免对手已到位却误超时
    if (!opponentPresent_ && !waitTimer_->isActive())
        waitTimer_->start();
}

void RoomWindow::stopWaitingTimeout()
{
    waitTimer_->stop();
}

// ---------- 槽 ----------
void RoomWindow::onReadyToggled(bool checked)
{
    selfReady_ = checked;
    updateStatus();
    emit readyRequested(checked);
}

void RoomWindow::onLeaveClicked()
{
    emit leaveRoomRequested();
}

void RoomWindow::onCountdownTick()
{
    --remainingSeconds_;
    if (remainingSeconds_ <= 0) {
        cancelCountdown();
        return;
    }
    countdownLabel_->setText(QStringLiteral("对局开始倒计时：%1 秒").arg(remainingSeconds_));
}

void RoomWindow::onWaitTimeout()
{
    waitTimer_->stop();
    emit waitTimedOut();
}

void RoomWindow::updateStatus()
{
    selfReadyLabel_->setText(selfReady_ ? QStringLiteral("已准备") : QStringLiteral("未准备"));
    opponentReadyLabel_->setText(opponentReady_ ? QStringLiteral("已准备") : QStringLiteral("未准备"));
}
