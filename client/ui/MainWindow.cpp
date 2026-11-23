#include "MainWindow.h"

#include <QShowEvent>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSplitter>
#include <QTableWidget>
#include <QListWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QHeaderView>
#include <QDateTime>
#include <QMessageBox>
#include <QDialog>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QStringList>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
}

void MainWindow::showEvent(QShowEvent* event)
{
    QMainWindow::showEvent(event);
    // 与 LoginWindow 同样的焦点问题：窗口未激活时首次点击聊天框只激活窗口、
    // 不聚焦 QLineEdit，中文输入法态下表现为"吞键/打不进字"。
    // 这里主动激活并把焦点交给聊天输入框；不禁用 IME，保留中文聊天能力。
    activateWindow();
    raise();
    if (chatInput_)
        chatInput_->setFocus(Qt::ActiveWindowFocusReason);
}

void MainWindow::setupUi()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    // ---------- 顶部用户信息 + 入口 ----------
    userInfoLabel_ = new QLabel(QStringLiteral("未登录"), central);
    rankBtn_     = new QPushButton(QStringLiteral("排行榜"), central);
    shopBtn_     = new QPushButton(QStringLiteral("商城"), central);
    offlineBtn_  = new QPushButton(QStringLiteral("离线对战"), central);
    logoutBtn_   = new QPushButton(QStringLiteral("退出登录"), central);

    QHBoxLayout *top = new QHBoxLayout;
    top->addWidget(userInfoLabel_, 1);
    top->addWidget(rankBtn_);
    top->addWidget(shopBtn_);
    top->addWidget(offlineBtn_);
    top->addWidget(logoutBtn_);

    // ---------- 左：房间列表 ----------
    roomTable_ = new QTableWidget(0, 5, central);
    roomTable_->setHorizontalHeaderLabels(
        QStringList() << QStringLiteral("房间ID") << QStringLiteral("房主")
                      << QStringLiteral("ELO") << QStringLiteral("状态")
                      << QStringLiteral("密码"));
    roomTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    roomTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    roomTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    roomTable_->setSelectionMode(QAbstractItemView::SingleSelection);

    refreshBtn_     = new QPushButton(QStringLiteral("刷新"), central);
    createRoomBtn_  = new QPushButton(QStringLiteral("创建房间"), central);
    joinRoomBtn_    = new QPushButton(QStringLiteral("加入房间"), central);
    randomJoinBtn_  = new QPushButton(QStringLiteral("随机匹配"), central);
    prevPageBtn_    = new QPushButton(QStringLiteral("上一页"), central);
    nextPageBtn_    = new QPushButton(QStringLiteral("下一页"), central);
    pageSizeSpin_   = new QSpinBox(central);
    pageSizeSpin_->setRange(5, 100);
    pageSizeSpin_->setValue(20);
    pageSizeSpin_->setSuffix(QStringLiteral(" 条/页"));
    pageLabel_ = new QLabel(QStringLiteral("第 1 / 1 页"), central);

    QHBoxLayout *roomBtns = new QHBoxLayout;
    roomBtns->addWidget(refreshBtn_);
    roomBtns->addWidget(createRoomBtn_);
    roomBtns->addWidget(joinRoomBtn_);
    roomBtns->addWidget(randomJoinBtn_);

    QHBoxLayout *pageRow = new QHBoxLayout;
    pageRow->addWidget(prevPageBtn_);
    pageRow->addWidget(nextPageBtn_);
    pageRow->addWidget(pageSizeSpin_);
    pageRow->addWidget(pageLabel_, 1);

    QWidget *roomPanel = new QWidget(central);
    QVBoxLayout *roomLayout = new QVBoxLayout(roomPanel);
    roomLayout->addWidget(roomTable_, 1);
    roomLayout->addLayout(roomBtns);
    roomLayout->addLayout(pageRow);

    // ---------- 右：在线玩家 ----------
    playerList_ = new QListWidget(central);
    QWidget *playerPanel = new QWidget(central);
    QVBoxLayout *playerLayout = new QVBoxLayout(playerPanel);
    playerLayout->addWidget(new QLabel(QStringLiteral("在线玩家"), playerPanel));
    playerLayout->addWidget(playerList_, 1);

    QSplitter *lists = new QSplitter(central);
    lists->addWidget(roomPanel);
    lists->addWidget(playerPanel);
    lists->setStretchFactor(0, 3);
    lists->setStretchFactor(1, 1);

    // ---------- 底：聊天 ----------
    chatDisplay_ = new QTextEdit(central);
    chatDisplay_->setReadOnly(true);
    chatInput_ = new QLineEdit(central);
    chatInput_->setPlaceholderText(QStringLiteral("输入聊天内容，回车发送"));
    sendChatBtn_ = new QPushButton(QStringLiteral("发送"), central);

    QHBoxLayout *chatRow = new QHBoxLayout;
    chatRow->addWidget(chatInput_, 1);
    chatRow->addWidget(sendChatBtn_);

    QWidget *chatPanel = new QWidget(central);
    QVBoxLayout *chatLayout = new QVBoxLayout(chatPanel);
    chatLayout->addWidget(new QLabel(QStringLiteral("大厅聊天"), chatPanel));
    chatLayout->addWidget(chatDisplay_, 1);
    chatLayout->addLayout(chatRow);

    QVBoxLayout *main = new QVBoxLayout(central);
    main->addLayout(top);
    main->addWidget(lists, 1);
    main->addWidget(chatPanel);

    setWindowTitle(QStringLiteral("游戏大厅 - AI-WuZiQi"));
    resize(960, 680);

    // ---------- 信号连接：全部抛给 controller ----------
    connect(refreshBtn_, &QPushButton::clicked, this, &MainWindow::onRefreshClicked);
    connect(createRoomBtn_, &QPushButton::clicked, this, &MainWindow::onCreateRoomClicked);
    connect(joinRoomBtn_, &QPushButton::clicked, this, &MainWindow::onJoinRoomClicked);
    connect(randomJoinBtn_, &QPushButton::clicked, this, &MainWindow::onRandomMatchClicked);
    connect(sendChatBtn_, &QPushButton::clicked, this, &MainWindow::onSendChatClicked);
    connect(chatInput_, &QLineEdit::returnPressed, this, &MainWindow::onSendChatClicked);
    connect(roomTable_, &QTableWidget::cellDoubleClicked, this, &MainWindow::onRoomDoubleClicked);
    connect(prevPageBtn_, &QPushButton::clicked, this, &MainWindow::onPrevPage);
    connect(nextPageBtn_, &QPushButton::clicked, this, &MainWindow::onNextPage);
    connect(rankBtn_, &QPushButton::clicked, this, &MainWindow::rankRequested);
    connect(shopBtn_, &QPushButton::clicked, this, &MainWindow::shopRequested);
    connect(offlineBtn_, &QPushButton::clicked, this, &MainWindow::offlineGameRequested);
    connect(logoutBtn_, &QPushButton::clicked, this, &MainWindow::logoutRequested);
}

// ---------- 数据回填 ----------
void MainWindow::setUserInfo(const QString &username, int elo, int wins, qint64 coins)
{
    userInfoLabel_->setText(
        QStringLiteral("用户：%1   ELO：%2   胜场：%3   金币：%4")
            .arg(username).arg(elo).arg(wins).arg(coins));
}

void MainWindow::setRooms(const QVector<RoomEntry> &rooms, int total, int page, int pageCount)
{
    currentPage_ = page;
    pageCount_ = qMax(1, pageCount_);
    roomTable_->setRowCount(rooms.size());
    for (int i = 0; i < rooms.size(); ++i) {
        const RoomEntry &r = rooms[i];
        roomTable_->setItem(i, 0, new QTableWidgetItem(QString::number(r.id)));
        roomTable_->setItem(i, 1, new QTableWidgetItem(r.ownerName));
        roomTable_->setItem(i, 2, new QTableWidgetItem(QString::number(r.ownerElo)));
        roomTable_->setItem(i, 3, new QTableWidgetItem(r.status));
        roomTable_->setItem(i, 4, new QTableWidgetItem(r.hasPassword ? QStringLiteral("有") : QStringLiteral("无")));
        roomTable_->item(i, 0)->setData(Qt::UserRole, r.id);
    }
    updatePageLabel();
    Q_UNUSED(total);
}

void MainWindow::setPlayers(const QVector<PlayerEntry> &players)
{
    playerList_->clear();
    for (const PlayerEntry &p : players) {
        QString statusText = (p.status == QLatin1String("in_game"))
                           ? QStringLiteral("对局中") : QStringLiteral("在线");
        playerList_->addItem(QStringLiteral("%1  [ELO %2]  %3")
                                 .arg(p.name).arg(p.elo).arg(statusText));
    }
}

void MainWindow::appendChat(const QString &sender, const QString &content, qint64 timestamp)
{
    QString time;
    if (timestamp > 0)
        time = QDateTime::fromSecsSinceEpoch(timestamp).toString(QStringLiteral("HH:mm:ss"));
    else
        time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    chatDisplay_->append(QStringLiteral("[%1] %2: %3").arg(time, sender, content));
}

void MainWindow::clearChat()
{
    chatDisplay_->clear();
}

void MainWindow::updatePageLabel()
{
    pageLabel_->setText(QStringLiteral("第 %1 / %2 页").arg(currentPage_).arg(pageCount_));
}

// ---------- 槽 ----------
void MainWindow::onRefreshClicked()
{
    emit refreshRoomsRequested(currentPage_, pageSizeSpin_->value());
}

void MainWindow::onPrevPage()
{
    if (currentPage_ > 1) {
        --currentPage_;
        emit refreshRoomsRequested(currentPage_, pageSizeSpin_->value());
    }
}

void MainWindow::onNextPage()
{
    if (currentPage_ < pageCount_) {
        ++currentPage_;
        emit refreshRoomsRequested(currentPage_, pageSizeSpin_->value());
    }
}

void MainWindow::onCreateRoomClicked()
{
    // 用一个轻量对话框采集房间参数，参数变化只需在这里改，信号签名保持稳定
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("创建房间"));
    QSpinBox *total = new QSpinBox(&dlg);
    total->setRange(60, 1800);
    total->setValue(180);
    total->setSuffix(QStringLiteral(" 秒"));
    QSpinBox *step = new QSpinBox(&dlg);
    step->setRange(5, 300);
    step->setValue(30);
    step->setSuffix(QStringLiteral(" 秒"));
    QLineEdit *pwd = new QLineEdit(&dlg);
    pwd->setPlaceholderText(QStringLiteral("房间密码（可留空）"));
    QFormLayout *form = new QFormLayout(&dlg);
    form->addRow(QStringLiteral("总时限"), total);
    form->addRow(QStringLiteral("每步时限"), step);
    form->addRow(QStringLiteral("密码"), pwd);
    QDialogButtonBox *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() == QDialog::Accepted)
        emit createRoomRequested(total->value(), step->value(), pwd->text());
}

void MainWindow::onJoinRoomClicked()
{
    QTableWidgetItem *item = roomTable_->item(roomTable_->currentRow(), 0);
    if (!item) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("请先选择一个房间"));
        return;
    }
    qint64 roomId = item->data(Qt::UserRole).toLongLong();
    QString pwd;
    // 若房间需要密码，弹出输入框
    QTableWidgetItem *pwItem = roomTable_->item(roomTable_->currentRow(), 4);
    if (pwItem && pwItem->text() == QStringLiteral("有")) {
        bool ok = false;
        pwd = QInputDialog::getText(this, QStringLiteral("房间密码"),
                                    QStringLiteral("请输入房间密码："),
                                    QLineEdit::Password, QString(), &ok);
        if (!ok)
            return;
    }
    emit joinRoomRequested(roomId, pwd);
}

void MainWindow::onRandomMatchClicked()
{
    emit randomMatchRequested();
}

void MainWindow::onRoomDoubleClicked(int row, int column)
{
    Q_UNUSED(column);
    QTableWidgetItem *item = roomTable_->item(row, 0);
    if (!item)
        return;
    qint64 roomId = item->data(Qt::UserRole).toLongLong();
    QString pwd;
    QTableWidgetItem *pwItem = roomTable_->item(row, 4);
    if (pwItem && pwItem->text() == QStringLiteral("有")) {
        bool ok = false;
        pwd = QInputDialog::getText(this, QStringLiteral("房间密码"),
                                    QStringLiteral("请输入房间密码："),
                                    QLineEdit::Password, QString(), &ok);
        if (!ok)
            return;
    }
    emit joinRoomRequested(roomId, pwd);
}

void MainWindow::onSendChatClicked()
{
    QString text = chatInput_->text().trimmed();
    if (text.isEmpty())
        return;
    emit chatRequested(text);
    chatInput_->clear();
}
