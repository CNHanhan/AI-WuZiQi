#include "GameWindow.h"
#include "GomokuBoardWidget.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QTimer>
#include <QMessageBox>
#include <QFont>

GameWindow::GameWindow(QWidget *parent)
    : QMainWindow(parent)
    , gameTimer_(new QTimer(this))
{
    for (int i = 0; i < AIWZQ::BOARD_SIZE; ++i)
        for (int j = 0; j < AIWZQ::BOARD_SIZE; ++j)
            grid_[i][j] = AIWZQ::Color::EMPTY;
    setupUi();

    gameTimer_->setInterval(1000);
    connect(gameTimer_, &QTimer::timeout, this, &GameWindow::onTimerTick);
}

void GameWindow::setupUi()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    boardWidget_ = new GomokuBoardWidget(central);

    blackInfoLabel_ = new QLabel(QStringLiteral("黑方：--"), central);
    whiteInfoLabel_ = new QLabel(QStringLiteral("白方：--"), central);
    blackTimeBar_ = new QProgressBar(central);
    whiteTimeBar_ = new QProgressBar(central);
    blackTimeBar_->setRange(0, 180);
    whiteTimeBar_->setRange(0, 180);
    blackTimeBar_->setValue(180);
    whiteTimeBar_->setValue(180);
    stepTimerLabel_ = new QLabel(QStringLiteral("每步：--"), central);
    statusLabel_ = new QLabel(QStringLiteral(""), central);

    undoBtn_ = new QPushButton(QStringLiteral("悔棋"), central);
    drawBtn_ = new QPushButton(QStringLiteral("和棋"), central);
    surrenderBtn_ = new QPushButton(QStringLiteral("认输"), central);

    QVBoxLayout *infoPanel = new QVBoxLayout;
    infoPanel->addWidget(blackInfoLabel_);
    infoPanel->addWidget(blackTimeBar_);
    infoPanel->addWidget(whiteInfoLabel_);
    infoPanel->addWidget(whiteTimeBar_);
    infoPanel->addSpacing(10);
    QFont f = stepTimerLabel_->font();
    f.setPointSize(16);
    f.setBold(true);
    stepTimerLabel_->setFont(f);
    infoPanel->addWidget(stepTimerLabel_);
    infoPanel->addWidget(statusLabel_);
    infoPanel->addStretch(1);
    infoPanel->addWidget(undoBtn_);
    infoPanel->addWidget(drawBtn_);
    infoPanel->addWidget(surrenderBtn_);

    QHBoxLayout *main = new QHBoxLayout(central);
    main->addWidget(boardWidget_, 1);
    main->addLayout(infoPanel);

    setWindowTitle(QStringLiteral("对局 - AI-WuZiQi"));
    resize(900, 640);

    connect(boardWidget_, &GomokuBoardWidget::clickAt, this, &GameWindow::onBoardClicked);
    connect(undoBtn_, &QPushButton::clicked, this, &GameWindow::onUndoRequested);
    connect(drawBtn_, &QPushButton::clicked, this, &GameWindow::onDrawRequested);
    connect(surrenderBtn_, &QPushButton::clicked, this, &GameWindow::onSurrender);
}

// ---------- 对局初始化 ----------
void GameWindow::startGame(int myColor,
                           const QString &myName,
                           const QString &opponentName,
                           int totalTimeSec,
                           int stepTimeSec)
{
    myColor_ = myColor == AIWZQ::Color::WHITE ? AIWZQ::Color::WHITE : AIWZQ::Color::BLACK;
    myName_ = myName;
    opponentName_ = opponentName;
    totalMax_ = totalTimeSec;
    blackTotalLeft_ = totalTimeSec;
    whiteTotalLeft_ = totalTimeSec;
    stepTimeLeft_ = stepTimeSec;
    gameOver_ = false;

    blackTimeBar_->setRange(0, totalMax_);
    whiteTimeBar_->setRange(0, totalMax_);
    blackTimeBar_->setValue(totalMax_);
    whiteTimeBar_->setValue(totalMax_);

    clearBoard();
    refreshInfoLabels();
    // 黑先手：若我是黑方则我先落子
    setTurn(myColor_ == AIWZQ::Color::BLACK);
    statusLabel_->setText(QStringLiteral("对局开始"));

    if (!gameTimer_->isActive())
        gameTimer_->start();
}

// ---------- 外部数据回填 ----------
void GameWindow::placeStoneAt(int x, int y, int color, bool playEffect)
{
    boardWidget_->setCell(x, y, color);
    if (x >= 0 && x < AIWZQ::BOARD_SIZE && y >= 0 && y < AIWZQ::BOARD_SIZE)
        grid_[x][y] = color;
    lastMove_ = QPoint(x, y);
    boardWidget_->setLastMove(x, y);
    if (playEffect)
        boardWidget_->playPlaceEffect(x, y);
}

void GameWindow::removeLastStone()
{
    if (lastMove_.x() < 0 || lastMove_.y() < 0)
        return;
    int x = lastMove_.x(), y = lastMove_.y();
    grid_[x][y] = AIWZQ::Color::EMPTY;
    boardWidget_->setCell(x, y, AIWZQ::Color::EMPTY);
    lastMove_ = QPoint(-1, -1);
    boardWidget_->setLastMove(-1, -1);
    // 找历史里的上一手作为新的最后标记（简单回退：清空标记，controller 可再 setLastMove）
}

void GameWindow::setLastMove(int x, int y)
{
    lastMove_ = QPoint(x, y);
    boardWidget_->setLastMove(x, y);
}

void GameWindow::updateTimes(int blackTotalLeft, int whiteTotalLeft, int stepLeft)
{
    blackTotalLeft_ = blackTotalLeft;
    whiteTotalLeft_ = whiteTotalLeft;
    stepTimeLeft_ = stepLeft;
    blackTimeBar_->setValue(qMax(0, blackTotalLeft_));
    whiteTimeBar_->setValue(qMax(0, whiteTotalLeft_));
    stepTimerLabel_->setText(QStringLiteral("每步剩余：%1 秒").arg(qMax(0, stepTimeLeft_)));
}

void GameWindow::setTurn(bool myTurn)
{
    isMyTurn_ = myTurn;
    boardWidget_->setInteractive(myTurn && !gameOver_);
}

void GameWindow::clearBoard()
{
    for (int i = 0; i < AIWZQ::BOARD_SIZE; ++i)
        for (int j = 0; j < AIWZQ::BOARD_SIZE; ++j)
            grid_[i][j] = AIWZQ::Color::EMPTY;
    lastMove_ = QPoint(-1, -1);
    boardWidget_->clearBoard();
}

void GameWindow::endGame(int winnerColor, const QString &reason, const QString &detail)
{
    gameOver_ = true;
    gameTimer_->stop();
    boardWidget_->setInteractive(false);

    QString result;
    if (winnerColor == AIWZQ::Color::EMPTY)
        result = QStringLiteral("和棋");
    else
        result = QStringLiteral("%1 获胜").arg(colorName(winnerColor));

    QString reasonText;
    if (reason == QLatin1String("draw"))       reasonText = QStringLiteral("和棋");
    else if (reason == QLatin1String("surrender")) reasonText = QStringLiteral("认输");
    else if (reason == QLatin1String("timeout"))   reasonText = QStringLiteral("超时");
    else                                          reasonText = QStringLiteral("五连");

    QString msg = QStringLiteral("%1（%2）").arg(result, reasonText);
    if (!detail.isEmpty())
        msg += QStringLiteral(" — %1").arg(detail);
    statusLabel_->setText(msg);
    showMessage(msg);
}

void GameWindow::showMessage(const QString &msg)
{
    statusLabel_->setText(msg);
}

// ---------- 槽 ----------
void GameWindow::onBoardClicked(int x, int y)
{
    if (gameOver_ || !isMyTurn_)
        return;
    if (x < 0 || x >= AIWZQ::BOARD_SIZE || y < 0 || y >= AIWZQ::BOARD_SIZE)
        return;
    if (grid_[x][y] != AIWZQ::Color::EMPTY)
        return;
    emit placeStoneRequested(x, y);
}

void GameWindow::onTimerTick()
{
    if (gameOver_)
        return;
    // 每秒本地扣减展示值；服务端 place_stone_notify 会用权威值覆盖
    stepTimeLeft_ = qMax(0, stepTimeLeft_ - 1);
    if (isMyTurn_) {
        if (myColor_ == AIWZQ::Color::BLACK)
            blackTotalLeft_ = qMax(0, blackTotalLeft_ - 1);
        else
            whiteTotalLeft_ = qMax(0, whiteTotalLeft_ - 1);
    }
    blackTimeBar_->setValue(blackTotalLeft_);
    whiteTimeBar_->setValue(whiteTotalLeft_);
    stepTimerLabel_->setText(QStringLiteral("每步剩余：%1 秒").arg(stepTimeLeft_));
}

void GameWindow::onUndoRequested()
{
    emit undoRequested();
}

void GameWindow::onDrawRequested()
{
    emit drawRequested();
}

void GameWindow::onSurrender()
{
    emit surrenderRequested();
}

// ---------- 辅助 ----------
void GameWindow::refreshInfoLabels()
{
    QString blackName = (myColor_ == AIWZQ::Color::BLACK) ? myName_ : opponentName_;
    QString whiteName = (myColor_ == AIWZQ::Color::WHITE) ? myName_ : opponentName_;
    blackInfoLabel_->setText(QStringLiteral("黑方：%1").arg(blackName));
    whiteInfoLabel_->setText(QStringLiteral("白方：%1").arg(whiteName));
}

QString GameWindow::colorName(int color) const
{
    return (color == AIWZQ::Color::BLACK) ? QStringLiteral("黑方") : QStringLiteral("白方");
}
