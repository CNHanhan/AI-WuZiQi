#include "GameController.h"

#include "TcpClient.h"
#include "AIManager.h"
#include "CryptoManager.h"
#include "ProtocolCodec.h"
#include "SecureStorage.h"

// UI 的 LoginWindow.h 内 FindPasswordDialog 继承 QDialog 却只做了前向声明，
// 此处先包含完整定义以规避（不改 UI 文件）。
#include <QDialog>
#include "ui/LoginWindow.h"
#include "ui/MainWindow.h"
#include "ui/RoomWindow.h"
#include "ui/GameWindow.h"
#include "ui/ShopWindow.h"
#include "ui/RankDialog.h"

#include <QJsonDocument>
#include <QDebug>
#include <QDateTime>
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QTimer>
#include <QMessageBox>

namespace {
// 把窗口居中显示（QMainWindow 自身无居中逻辑）
void centerAndShow(QWidget* w)
{
    if (!w) return;
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        const QRect frame = w->frameGeometry().isEmpty() ? w->geometry() : w->frameGeometry();
        w->move(avail.center() - frame.center());
    }
    w->show();
    w->raise();
    w->activateWindow();
}

// 仅设置初始居中位置，不 show：避免窗口首次显示前停在屏幕边角，
// show/切换时仍由 centerAndShow 负责最终居中。
void placeWindowCentered(QWidget* w)
{
    if (!w) return;
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        const QRect frame = w->frameGeometry().isEmpty() ? w->geometry() : w->frameGeometry();
        w->move(avail.center() - frame.center());
    }
}
} // namespace

GameController::GameController(QObject* parent)
    : QObject(parent)
{
    codec_  = new ProtocolCodec(this);
    crypto_ = new CryptoManager(this);
    tcp_    = new TcpClient(this);
    ai_     = new AIManager(this);

    // 随机匹配等待对手 30 秒超时定时器（单次触发）
    matchWaitTimer_ = new QTimer(this);
    matchWaitTimer_->setSingleShot(true);
    matchWaitTimer_->setInterval(30 * 1000);
    connect(matchWaitTimer_, &QTimer::timeout, this, &GameController::onMatchWaitTimedOut);

    connect(tcp_, &TcpClient::messageReceived, this, &GameController::onMessageReceived);
    connect(tcp_, &TcpClient::connected,       this, &GameController::onConnected);
    connect(tcp_, &TcpClient::disconnected,    this, &GameController::onDisconnected);
    connect(tcp_, QOverload<QAbstractSocket::SocketError>::of(&TcpClient::error),
            this, &GameController::onSocketError);

    connect(ai_, &AIManager::aiMoveReady,  this, &GameController::onAiMoveReady);
    connect(ai_, &AIManager::gameOver,     this, &GameController::onOfflineGameOver);
}

GameController::~GameController() = default;

// =============== 窗口创建 / 接线 ===============
void GameController::createWindows()
{
    loginWindow_ = new LoginWindow();
    mainWindow_  = new MainWindow();
    roomWindow_  = new RoomWindow();
    gameWindow_  = new GameWindow();
    shopWindow_  = new ShopWindow();
    rankDialog_  = new RankDialog();

    // ---- LoginWindow ----
    connect(loginWindow_, &LoginWindow::loginRequested,    this, &GameController::onLoginRequested);
    connect(loginWindow_, &LoginWindow::registerRequested, this, &GameController::onRegisterRequested);
    connect(loginWindow_, &LoginWindow::findPwdRequested,  this, &GameController::onFindPwdRequested);
    connect(loginWindow_, &LoginWindow::verifyCodeRequested, this, &GameController::onVerifyCodeRequested);
    connect(loginWindow_, &LoginWindow::resetPwdRequested, this, &GameController::onResetPwdRequested);
    connect(loginWindow_, &LoginWindow::offlineGameRequested, this, &GameController::onOfflineGameRequested);

    // ---- MainWindow ----
    connect(mainWindow_, &MainWindow::refreshRoomsRequested,  this, &GameController::onRefreshRoomsRequested);
    connect(mainWindow_, &MainWindow::createRoomRequested,    this, &GameController::onCreateRoomRequested);
    connect(mainWindow_, &MainWindow::joinRoomRequested,      this, &GameController::onJoinRoomRequested);
    connect(mainWindow_, &MainWindow::randomMatchRequested,   this, &GameController::onRandomMatchRequested);
    connect(mainWindow_, &MainWindow::chatRequested,          this, &GameController::onChatRequested);
    connect(mainWindow_, &MainWindow::rankRequested,          this, &GameController::onRankRequested);
    connect(mainWindow_, &MainWindow::shopRequested,          this, &GameController::onShopRequested);
    connect(mainWindow_, &MainWindow::offlineGameRequested,   this, &GameController::onOfflineGameRequested);
    connect(mainWindow_, &MainWindow::logoutRequested,        this, &GameController::onLogoutRequested);

    // ---- RoomWindow ----
    connect(roomWindow_, &RoomWindow::readyRequested,    this, &GameController::onReadyRequested);
    connect(roomWindow_, &RoomWindow::leaveRoomRequested,this, &GameController::onLeaveRoomRequested);
    connect(roomWindow_, &RoomWindow::waitTimedOut,      this, &GameController::onRoomWaitTimedOut);

    // ---- GameWindow ----
    connect(gameWindow_, &GameWindow::placeStoneRequested,this, &GameController::onPlaceStoneRequested);
    connect(gameWindow_, &GameWindow::undoRequested,      this, &GameController::onUndoRequested);
    connect(gameWindow_, &GameWindow::drawRequested,      this, &GameController::onDrawRequested);
    connect(gameWindow_, &GameWindow::surrenderRequested, this, &GameController::onSurrenderRequested);
    connect(gameWindow_, &GameWindow::readyRequested,     this, &GameController::onGameReadyRequested);
    connect(gameWindow_, &GameWindow::leaveGameRequested, this, &GameController::onLeaveGameRequested);

    // ---- ShopWindow ----
    connect(shopWindow_, &ShopWindow::buyItemRequested,   this, &GameController::onBuyItemRequested);
    connect(shopWindow_, &ShopWindow::equipItemRequested, this, &GameController::onEquipItemRequested);
    connect(shopWindow_, &ShopWindow::requestShopList,    this, &GameController::onRequestShopList);

    // ---- RankDialog ----
    connect(rankDialog_, &RankDialog::rankRequested, this, &GameController::onRankQueryRequested);

    // ---- 初始居中：仅 move 不 show，避免首次显示前闪现在边角 ----
    placeWindowCentered(loginWindow_);
    placeWindowCentered(mainWindow_);
    placeWindowCentered(roomWindow_);
    placeWindowCentered(gameWindow_);
    placeWindowCentered(shopWindow_);
    placeWindowCentered(rankDialog_);
}

void GameController::showAllWindows() { centerAndShow(loginWindow_); }

void GameController::setServer(const QString& host, quint16 port)
{
    host_ = host; port_ = port;
}

void GameController::start()
{
    setState(Connecting);
    emit statusMessage(QStringLiteral("正在连接服务器..."));
    tcp_->connectToServer(host_, port_);
}

void GameController::setState(State s)
{
    if (state_ == s) return;
    state_ = s;
    emit stateChanged(s);
}

// =============== 发送辅助 ===============
void GameController::send(const QString& type, const QJsonObject& data)
{
    const AIWZQ::Message m = codec_->build(type, data);
    updateLastSeq(m.seq);
    tcp_->sendMessage(m);
}

void GameController::sendSensitive(const QString& type, QJsonObject data)
{
    // 密码以明文传输（TLS 1.3 链路已加密），由服务端统一做 Argon2id 哈希与校验。
    // 之前客户端先哈希再传、服务端又哈希+校验格式，导致哈希串含 '$' 等字符
    // 通不过服务端 validatePassword，注册/登录永远"输入参数无效"。
    // 注意：不要在这里对 password 做任何哈希。
    AIWZQ::Message m = codec_->build(type, data);
    if (sessionKey_.size() == 32) {
        const QByteArray plain = QJsonDocument(codec_->toQJson(m)).toJson(QJsonDocument::Compact);
        const QByteArray enc = CryptoManager::encryptThenBase64(plain, sessionKey_);
        QJsonObject env;
        env.insert(QStringLiteral("type"), QStringLiteral("secure_envelope"));
        env.insert(QStringLiteral("seq"), QJsonValue(m.seq));
        env.insert(QStringLiteral("timestamp"), QJsonValue(m.timestamp));
        env.insert(QStringLiteral("nonce"), QString::fromStdString(m.nonce));
        env.insert(QStringLiteral("payload"), QString::fromUtf8(enc));
        env.insert(QStringLiteral("sig"), CryptoManager::signMessage(sessionKey_, plain));
        m = codec_->build(QStringLiteral("secure_envelope"), env);
    }
    updateLastSeq(m.seq);
    tcp_->sendMessage(m);
}

void GameController::updateLastSeq(qint64 seq)
{
    if (seq > lastSeq_) lastSeq_ = seq;
}

void GameController::showLoginAndHideOthers()
{
    if (mainWindow_)  mainWindow_->hide();
    if (roomWindow_)  roomWindow_->hide();
    if (gameWindow_)  gameWindow_->hide();
    if (shopWindow_)  shopWindow_->hide();
    if (rankDialog_)  rankDialog_->hide();
    centerAndShow(loginWindow_);
}

// =============== 登录/注册/找回/离线 ===============
void GameController::onLoginRequested(const QString& username, const QString& password)
{
    if (loginWindow_) loginWindow_->setBusy(true);
    QJsonObject d;
    d.insert(QStringLiteral("username"), username);
    d.insert(QStringLiteral("password"), password);
    d.insert(QStringLiteral("client_version"), QStringLiteral("1.0.0"));
    sendSensitive(AIWZQ::MsgType::LOGIN_REQ, d);
    emit statusMessage(QStringLiteral("正在登录..."));
}

void GameController::onRegisterRequested(const QString& username, const QString& password, const QString& email)
{
    if (loginWindow_) loginWindow_->setBusy(true);
    QJsonObject d;
    d.insert(QStringLiteral("username"), username);
    d.insert(QStringLiteral("password"), password);
    d.insert(QStringLiteral("email"), email);
    sendSensitive(AIWZQ::MsgType::REGISTER_REQ, d);
}

void GameController::onFindPwdRequested(const QString& email)
{
    Q_UNUSED(email);   // UI 已打开 FindPasswordDialog，由其 verifyCode/resetPwd 驱动
}

void GameController::onVerifyCodeRequested(const QString& email)
{
    QJsonObject d;
    d.insert(QStringLiteral("email"), email);
    send(AIWZQ::MsgType::FIND_PWD_REQ, d);   // 第一阶段：发送验证码
}

void GameController::onResetPwdRequested(const QString& email, const QString& code, const QString& newPassword)
{
    QJsonObject d;
    d.insert(QStringLiteral("email"), email);
    d.insert(QStringLiteral("code"), code);
    d.insert(QStringLiteral("password"), newPassword);
    sendSensitive(AIWZQ::MsgType::CHANGE_PWD_REQ, d);   // 第二阶段：重置密码
}

void GameController::onOfflineGameRequested()
{
    offlineDifficulty_ = 2;
    setState(InOfflineGame);
    board_.clear();
    if (mainWindow_)  mainWindow_->hide();
    if (loginWindow_) loginWindow_->hide();
    centerAndShow(gameWindow_);
    gameWindow_->clearBoard();
    const QString myName = userInfo_.username.empty()
                           ? QStringLiteral("玩家")
                           : QString::fromStdString(userInfo_.username);
    gameWindow_->startGame(AIWZQ::Color::BLACK, myName,
                           QStringLiteral("AI"), 180, 30);
    gameWindow_->setTurn(true);
    ai_->startGame(offlineDifficulty_, AIWZQ::Color::BLACK);  // 玩家执黑
    emit statusMessage(QStringLiteral("离线 AI 对局开始（难度中等）"));
}

// =============== 大厅 ===============
void GameController::onRefreshRoomsRequested(int page, int pageSize)
{
    requestRoomList(page, pageSize);
}

void GameController::requestRoomList(int page, int pageSize)
{
    QJsonObject d;
    d.insert(QStringLiteral("page"), page);
    d.insert(QStringLiteral("page_size"), pageSize);
    send(AIWZQ::MsgType::ROOM_LIST_REQ, d);
}

void GameController::onCreateRoomRequested(int totalTime, int stepTime, const QString& password)
{
    QJsonObject d;
    d.insert(QStringLiteral("total_time"), totalTime);
    d.insert(QStringLiteral("step_time"),  stepTime);
    d.insert(QStringLiteral("password"),   password);
    send(AIWZQ::MsgType::CREATE_ROOM_REQ, d);
}

void GameController::onJoinRoomRequested(qint64 roomId, const QString& password)
{
    QJsonObject d;
    d.insert(QStringLiteral("room_id"), QJsonValue(roomId));
    d.insert(QStringLiteral("password"), password);
    send(AIWZQ::MsgType::JOIN_ROOM_REQ, d);
}

void GameController::onRandomMatchRequested()
{
    send(AIWZQ::MsgType::RANDOM_JOIN_REQ, QJsonObject());
    // 启动 30 秒匹配等待超时；收到 room_info_resp（匹配成功）时停止
    if (matchWaitTimer_)
        matchWaitTimer_->start();
}

void GameController::onChatRequested(const QString& content)
{
    QJsonObject d;
    d.insert(QStringLiteral("content"), content);
    send(AIWZQ::MsgType::HALL_CHAT_REQ, d);
}

void GameController::onRankRequested()
{
    centerAndShow(rankDialog_);
    rankDialog_->rankRequested(QStringLiteral("elo"), 1);
}

void GameController::onRankQueryRequested(const QString& rankType, int page)
{
    QJsonObject d;
    d.insert(QStringLiteral("rank_type"), rankType);
    d.insert(QStringLiteral("page"), page);
    send(AIWZQ::MsgType::RANK_LIST_REQ, d);
}

void GameController::onShopRequested()
{
    send(AIWZQ::MsgType::SHOP_LIST_REQ, QJsonObject());
}

void GameController::onRequestShopList(const QString& type)
{
    QJsonObject d;
    if (!type.isEmpty()) d.insert(QStringLiteral("type"), type);
    send(AIWZQ::MsgType::SHOP_LIST_REQ, d);
}

void GameController::onLogoutRequested()
{
    send(AIWZQ::MsgType::LOGOUT_REQ, QJsonObject());
    token_.clear();
    SecureStorage::deleteToken();
    setState(Connected);
    showLoginAndHideOthers();
}

// =============== 房间 ===============
void GameController::onReadyRequested(bool ready)
{
    QJsonObject d; d.insert(QStringLiteral("ready"), ready);
    send(AIWZQ::MsgType::READY_REQ, d);
}

void GameController::onLeaveRoomRequested()
{
    // 协议无专用离开房间请求：用 LOGOUT 之外的通用方式，回到大厅并刷新
    if (matchWaitTimer_) matchWaitTimer_->stop();
    if (roomWindow_) roomWindow_->stopWaitingTimeout();
    if (mainWindow_) mainWindow_->show();
    if (roomWindow_) roomWindow_->hide();
    setState(InLobby);
    requestRoomList();
}

void GameController::onRoomWaitTimedOut()
{
    // 房间等待对手超过 150 秒：提示并退回大厅
    emit statusMessage(QStringLiteral("房间等待超时，已返回大厅"));
    QMessageBox::information(roomWindow_, QStringLiteral("房间等待超时"),
                             QStringLiteral("等待对手超过 2.5 分钟，房间已解散，返回大厅。"));
    if (matchWaitTimer_) matchWaitTimer_->stop();
    if (roomWindow_) roomWindow_->hide();
    if (mainWindow_) mainWindow_->show();
    setState(InLobby);
    requestRoomList();
}

void GameController::onMatchWaitTimedOut()
{
    // 随机匹配 30 秒内未匹配到对手：仅在仍处于大厅/匹配等待态时生效
    if (state_ != InLobby) {
        if (matchWaitTimer_) matchWaitTimer_->stop();
        return;
    }
    emit statusMessage(QStringLiteral("匹配超时，已取消匹配"));
    QMessageBox::information(mainWindow_, QStringLiteral("匹配超时"),
                             QStringLiteral("30 秒内未匹配到对手，已取消匹配。"));
    if (matchWaitTimer_) matchWaitTimer_->stop();
    if (roomWindow_) roomWindow_->hide();
    if (mainWindow_) mainWindow_->show();
    setState(InLobby);
    requestRoomList();
}

// =============== 对局 ===============
void GameController::onPlaceStoneRequested(int x, int y)
{
    if (state_ == InOfflineGame) {
        if (board_.placeStone(x, y, AIWZQ::Color::BLACK)) {
            gameWindow_->setLastMove(x, y);
            gameWindow_->setTurn(false);
            ai_->onPlayerMove(x, y);
        }
        return;
    }
    QJsonObject d;
    d.insert(QStringLiteral("x"), x);
    d.insert(QStringLiteral("y"), y);
    d.insert(QStringLiteral("timestamp"), QDateTime::currentSecsSinceEpoch());
    send(AIWZQ::MsgType::PLACE_STONE_REQ, d);
}

void GameController::onUndoRequested()
{
    if (state_ == InOfflineGame) { ai_->undoLastMove(); gameWindow_->removeLastStone(); return; }
    send(AIWZQ::MsgType::UNDO_REQ, QJsonObject());
}

void GameController::onDrawRequested()
{
    if (state_ == InOfflineGame) {
        gameWindow_->endGame(0, QStringLiteral("draw"), QStringLiteral("和棋"));
        return;
    }
    send(AIWZQ::MsgType::DRAW_REQ, QJsonObject());
}

void GameController::onSurrenderRequested()
{
    if (state_ == InOfflineGame) {
        gameWindow_->endGame(AIWZQ::Color::WHITE, QStringLiteral("surrender"), QStringLiteral("你认输了"));
        return;
    }
    send(AIWZQ::MsgType::SURRENDER_REQ, QJsonObject());
}

void GameController::onGameReadyRequested(bool ready)
{
    onReadyRequested(ready);
}

void GameController::onLeaveGameRequested()
{
    if (gameWindow_) gameWindow_->hide();
    if (mainWindow_) mainWindow_->show();
    setState(InLobby);
    requestRoomList();
}

// =============== 商城 ===============
void GameController::onBuyItemRequested(qint64 itemId)
{
    QJsonObject d; d.insert(QStringLiteral("item_id"), QJsonValue(itemId));
    send(AIWZQ::MsgType::BUY_ITEM_REQ, d);
}

void GameController::onEquipItemRequested(qint64 itemId)
{
    QJsonObject d;
    d.insert(QStringLiteral("item_id"), QJsonValue(itemId));
    d.insert(QStringLiteral("equipped"), true);
    send(AIWZQ::MsgType::EQUIP_ITEM_REQ, d);
}

// =============== 网络回调 ===============
void GameController::onConnected()
{
    setState(Connected);
    emit statusMessage(QStringLiteral("已连接服务器"));
    if (!token_.isEmpty()) doReconnect();
}

void GameController::onDisconnected()
{
    setState(Offline);
    emit statusMessage(QStringLiteral("与服务器断开连接，正在重连..."));
}

void GameController::onSocketError(QAbstractSocket::SocketError)
{
    emit statusMessage(QStringLiteral("网络错误：%1").arg(tcp_->lastErrorString()));
}

void GameController::doReconnect()
{
    QJsonObject d;
    d.insert(QStringLiteral("token"), token_);
    d.insert(QStringLiteral("last_seq"), QJsonValue(lastSeq_));
    send(AIWZQ::MsgType::RECONNECT_REQ, d);
    reconnectPending_ = true;
}

// =============== 离线 AI 回调 ===============
void GameController::onAiMoveReady(int x, int y)
{
    gameWindow_->placeStoneAt(x, y, AIWZQ::Color::WHITE, true);
    gameWindow_->setLastMove(x, y);
    gameWindow_->setTurn(true);
}

void GameController::onOfflineGameOver(int result)
{
    const int winnerColor = (result == 1) ? AIWZQ::Color::BLACK
                          : (result == 2) ? AIWZQ::Color::WHITE : 0;
    const QString reason = (result == 3) ? QStringLiteral("draw") : QStringLiteral("win");
    const QString detail = (result == 1) ? QStringLiteral("你赢了！")
                         : (result == 2) ? QStringLiteral("AI 获胜")
                                         : QStringLiteral("平局");
    gameWindow_->endGame(winnerColor, reason, detail);
    setState(InLobby);
}

// =============== 消息分派 ===============
void GameController::onMessageReceived(const QJsonObject& msg)
{
    const QString type = msg.value(QStringLiteral("type")).toString();
    const qint64  seq  = static_cast<qint64>(msg.value(QStringLiteral("seq")).toDouble());
    updateLastSeq(seq);
    const QString nonce = msg.value(QStringLiteral("nonce")).toString();
    if (!nonce.isEmpty() && seenNonces_.contains(nonce)) return;
    if (!nonce.isEmpty()) seenNonces_.insert(nonce);
    const QJsonObject data = msg.value(QStringLiteral("data")).toObject();

    if      (type == AIWZQ::MsgType::LOGIN_RESP)         dispatchLoginResp(data);
    else if (type == AIWZQ::MsgType::REGISTER_RESP)      dispatchRegisterResp(data);
    else if (type == AIWZQ::MsgType::FIND_PWD_RESP)      dispatchFindPwdResp(data);
    else if (type == AIWZQ::MsgType::CHANGE_PWD_RESP)    dispatchFindPwdResp(data);
    else if (type == AIWZQ::MsgType::ROOM_LIST_RESP)     dispatchRoomListResp(data);
    else if (type == AIWZQ::MsgType::PLAYER_LIST_RESP)   dispatchPlayerListResp(data);
    else if (type == AIWZQ::MsgType::RANK_LIST_RESP)     dispatchRankListResp(data);
    else if (type == AIWZQ::MsgType::HALL_CHAT_NOTIFY)   dispatchHallChatNotify(data);
    else if (type == AIWZQ::MsgType::CREATE_ROOM_RESP)   dispatchCreateRoomResp(data);
    else if (type == AIWZQ::MsgType::JOIN_ROOM_RESP)     dispatchJoinRoomResp(data);
    else if (type == AIWZQ::MsgType::ROOM_INFO_RESP)     dispatchRoomInfoResp(data);
    else if (type == AIWZQ::MsgType::READY_RESP)         dispatchReadyResp(data);
    else if (type == AIWZQ::MsgType::GAME_START_NOTIFY)  dispatchGameStartNotify(data);
    else if (type == AIWZQ::MsgType::PLACE_STONE_NOTIFY) dispatchPlaceStoneNotify(data);
    else if (type == AIWZQ::MsgType::UNDO_RESP)          dispatchUndoResp(data);
    else if (type == AIWZQ::MsgType::DRAW_RESP)          dispatchDrawResp(data);
    else if (type == AIWZQ::MsgType::GAME_OVER_NOTIFY)   dispatchGameOverNotify(data);
    else if (type == AIWZQ::MsgType::TIMEOUT_NOTIFY)     dispatchTimeoutNotify(data);
    else if (type == AIWZQ::MsgType::SHOP_LIST_RESP)     dispatchShopListResp(data);
    else if (type == AIWZQ::MsgType::BUY_ITEM_RESP)      dispatchBuyItemResp(data);
    else if (type == AIWZQ::MsgType::EQUIP_ITEM_RESP)    dispatchEquipItemResp(data);
    else if (type == AIWZQ::MsgType::RECONNECT_RESP)     dispatchReconnectResp(data);
    else if (type == AIWZQ::MsgType::HEARTBEAT_RESP)     { /* 心跳由 TcpClient 处理 */ }
    else qWarning() << "[GameController] unhandled msg type:" << type;
}

void GameController::parseUserInfo(const QJsonObject& o)
{
    AIWZQ::UserInfo u;
    u.uid      = static_cast<int64_t>(o.value(QStringLiteral("uid")).toDouble());
    u.username = o.value(QStringLiteral("username")).toString().toStdString();
    u.eloScore = o.value(QStringLiteral("elo_score")).toInt();
    u.wins     = o.value(QStringLiteral("wins")).toInt();
    u.losses   = o.value(QStringLiteral("losses")).toInt();
    u.draws    = o.value(QStringLiteral("draws")).toInt();
    u.coins    = static_cast<int64_t>(o.value(QStringLiteral("coins")).toDouble());
    u.avatar   = o.value(QStringLiteral("avatar")).toString().toStdString();
    userInfo_  = u;
}

void GameController::dispatchLoginResp(const QJsonObject& data)
{
    if (loginWindow_) loginWindow_->setBusy(false);
    const int code = data.value(QStringLiteral("code")).toInt();
    const QString message = data.value(QStringLiteral("message")).toString();
    if (code != AIWZQ::ErrCode::OK) {
        if (loginWindow_) loginWindow_->showMessage(message);
        emit statusMessage(QStringLiteral("登录失败：%1").arg(message));
        return;
    }
    token_ = data.value(QStringLiteral("token")).toString();
    SecureStorage::saveToken(token_);
    parseUserInfo(data.value(QStringLiteral("user_info")).toObject());
    if (loginWindow_) loginWindow_->showMessage(QStringLiteral("登录成功"));
    mainWindow_->setUserInfo(QString::fromStdString(userInfo_.username),
                             userInfo_.eloScore, userInfo_.wins, userInfo_.coins);
    setState(InLobby);
    if (loginWindow_) loginWindow_->hide();
    centerAndShow(mainWindow_);
    requestRoomList();
    emit statusMessage(QStringLiteral("登录成功，欢迎 %1")
                       .arg(QString::fromStdString(userInfo_.username)));
}

void GameController::dispatchRegisterResp(const QJsonObject& data)
{
    if (loginWindow_) loginWindow_->setBusy(false);
    const int code = data.value(QStringLiteral("code")).toInt();
    const QString message = data.value(QStringLiteral("message")).toString();
    if (loginWindow_) loginWindow_->showMessage(message);
    emit statusMessage(code == AIWZQ::ErrCode::OK ? QStringLiteral("注册成功") : message);
}

void GameController::dispatchFindPwdResp(const QJsonObject& data)
{
    const int code = data.value(QStringLiteral("code")).toInt();
    const QString message = data.value(QStringLiteral("message")).toString();
    if (loginWindow_) loginWindow_->showMessage(message);
    emit statusMessage(code == AIWZQ::ErrCode::OK ? QStringLiteral("操作成功") : message);
}

void GameController::dispatchRoomListResp(const QJsonObject& data)
{
    const int total = data.value(QStringLiteral("total")).toInt();
    QVector<RoomEntry> rooms;
    const QJsonArray arr = data.value(QStringLiteral("rooms")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        RoomEntry e;
        e.id          = static_cast<qint64>(o.value(QStringLiteral("room_id")).toDouble());
        e.ownerName   = o.value(QStringLiteral("owner_name")).toString();
        e.ownerElo    = o.value(QStringLiteral("owner_elo")).toInt();
        e.status      = o.value(QStringLiteral("status")).toString();
        e.hasPassword = o.value(QStringLiteral("has_password")).toBool();
        e.createTime  = static_cast<qint64>(o.value(QStringLiteral("create_time")).toDouble());
        rooms.append(e);
    }
    // pageCount: 依据 total 与当前 pageSize(20) 计算
    const int pageSize = 20;
    const int pageCount = qMax(1, (total + pageSize - 1) / pageSize);
    mainWindow_->setRooms(rooms, total, 1, pageCount);
}

void GameController::dispatchPlayerListResp(const QJsonObject& data)
{
    QVector<PlayerEntry> players;
    QJsonArray arr = data.value(QStringLiteral("players")).toArray();
    if (arr.isEmpty()) arr = data.value(QStringLiteral("online_players")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        PlayerEntry e;
        e.name   = o.value(QStringLiteral("username")).toString();
        if (e.name.isEmpty()) e.name = o.value(QStringLiteral("name")).toString();
        e.elo    = o.value(QStringLiteral("elo_score")).toInt();
        e.status = o.value(QStringLiteral("status")).toString();
        players.append(e);
    }
    mainWindow_->setPlayers(players);
}

void GameController::dispatchRankListResp(const QJsonObject& data)
{
    QVector<RankEntry> entries;
    const QJsonArray arr = data.value(QStringLiteral("rankings")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        RankEntry e;
        e.rank     = o.value(QStringLiteral("rank")).toInt();
        e.username = o.value(QStringLiteral("username")).toString();
        e.eloScore = o.value(QStringLiteral("elo_score")).toInt();
        e.wins     = o.value(QStringLiteral("wins")).toInt();
        e.losses   = o.value(QStringLiteral("losses")).toInt();
        e.draws    = o.value(QStringLiteral("draws")).toInt();
        entries.append(e);
    }
    rankDialog_->setRankings(entries);
    centerAndShow(rankDialog_);
    emit statusMessage(QStringLiteral("排行榜已更新"));
}

void GameController::dispatchHallChatNotify(const QJsonObject& data)
{
    mainWindow_->appendChat(data.value(QStringLiteral("sender")).toString(),
                            data.value(QStringLiteral("content")).toString(),
                            static_cast<qint64>(data.value(QStringLiteral("timestamp")).toDouble()));
}

void GameController::dispatchCreateRoomResp(const QJsonObject& data)
{
    const int code = data.value(QStringLiteral("code")).toInt();
    if (code == AIWZQ::ErrCode::OK) {
        const qint64 roomId = static_cast<qint64>(data.value(QStringLiteral("room_id")).toDouble());
        roomWindow_->setRoomId(roomId);
        roomWindow_->setSelfPlayer(QString::fromStdString(userInfo_.username), userInfo_.eloScore);
        setState(InRoom);
        if (mainWindow_) mainWindow_->hide();
        centerAndShow(roomWindow_);
        // 创建房间后开始等待对手的 150 秒超时
        roomWindow_->startWaitingTimeout();
    } else {
        emit statusMessage(data.value(QStringLiteral("message")).toString());
    }
}

void GameController::dispatchJoinRoomResp(const QJsonObject& data)
{
    const int code = data.value(QStringLiteral("code")).toInt();
    if (code == AIWZQ::ErrCode::OK) {
        setState(InRoom);
        if (mainWindow_) mainWindow_->hide();
        centerAndShow(roomWindow_);
        // 加入房间后开始等待对手的 150 秒超时（若对手已在则随后 setOpponentPlayer 会停掉）
        roomWindow_->startWaitingTimeout();
    } else {
        emit statusMessage(data.value(QStringLiteral("message")).toString());
    }
}

void GameController::dispatchRoomInfoResp(const QJsonObject& data)
{
    // 收到房间信息（随机匹配成功或房间状态更新）：取消 30 秒匹配等待超时
    if (matchWaitTimer_) matchWaitTimer_->stop();
    if (data.contains(QStringLiteral("room_id")))
        roomWindow_->setRoomId(static_cast<qint64>(data.value(QStringLiteral("room_id")).toDouble()));
    const QJsonObject self  = data.value(QStringLiteral("self")).toObject();
    const QJsonObject oppo  = data.value(QStringLiteral("opponent")).toObject();
    if (!self.isEmpty())
        roomWindow_->setSelfPlayer(self.value(QStringLiteral("username")).toString(),
                                   self.value(QStringLiteral("elo_score")).toInt());
    if (oppo.isEmpty()) {
        roomWindow_->clearOpponent();
    } else {
        roomWindow_->setOpponentPlayer(oppo.value(QStringLiteral("username")).toString(),
                                      oppo.value(QStringLiteral("elo_score")).toInt());
        roomWindow_->setOpponentReady(oppo.value(QStringLiteral("ready")).toBool());
    }
    roomWindow_->setSelfReady(data.value(QStringLiteral("self_ready")).toBool());
    if (data.contains(QStringLiteral("countdown")))
        roomWindow_->startCountdown(data.value(QStringLiteral("countdown")).toInt());
}

void GameController::dispatchReadyResp(const QJsonObject& data)
{
    // 服务端通常广播 room_info_resp；此处兜底刷新自身准备态
    if (data.contains(QStringLiteral("ready")))
        roomWindow_->setSelfReady(data.value(QStringLiteral("ready")).toBool());
}

void GameController::dispatchGameStartNotify(const QJsonObject& data)
{
    setState(InGame);
    if (matchWaitTimer_) matchWaitTimer_->stop();
    if (roomWindow_) { roomWindow_->stopWaitingTimeout(); roomWindow_->hide(); }
    centerAndShow(gameWindow_);
    myColor_   = data.value(QStringLiteral("my_color")).toInt();
    blackLeft_ = data.value(QStringLiteral("total_time_black")).toInt();
    whiteLeft_ = data.value(QStringLiteral("total_time_white")).toInt();
    stepLeft_  = 30;
    board_.clear();
    gameWindow_->clearBoard();
    gameWindow_->startGame(myColor_,
                           QString::fromStdString(userInfo_.username),
                           data.value(QStringLiteral("opponent_name")).toString(),
                           blackLeft_, stepLeft_);
    gameWindow_->setTurn(myColor_ == AIWZQ::Color::BLACK);
    gameWindow_->updateTimes(blackLeft_, whiteLeft_, stepLeft_);
    emit statusMessage(QStringLiteral("对局开始"));
}

void GameController::dispatchPlaceStoneNotify(const QJsonObject& data)
{
    const int x = data.value(QStringLiteral("x")).toInt();
    const int y = data.value(QStringLiteral("y")).toInt();
    const int color = data.value(QStringLiteral("color")).toInt();
    if (x >= 0 && x < AIWZQ::BOARD_SIZE && y >= 0 && y < AIWZQ::BOARD_SIZE)
        board_.placeStone(x, y, color);
    gameWindow_->placeStoneAt(x, y, color, true);
    gameWindow_->setLastMove(x, y);
    blackLeft_ = data.value(QStringLiteral("total_time_left")).toInt(blackLeft_);
    stepLeft_  = data.value(QStringLiteral("step_time_left")).toInt(stepLeft_);
    if (color == AIWZQ::Color::BLACK) blackLeft_ = data.value(QStringLiteral("total_time_left")).toInt(blackLeft_);
    else whiteLeft_ = data.value(QStringLiteral("total_time_left")).toInt(whiteLeft_);
    gameWindow_->updateTimes(blackLeft_, whiteLeft_, stepLeft_);
    const int uid = static_cast<int>(data.value(QStringLiteral("uid")).toDouble());
    gameWindow_->setTurn(uid != userInfo_.uid);
    if (data.value(QStringLiteral("is_game_over")).toBool()) {
        const int winnerColor = (uid == userInfo_.uid) ? myColor_ : (myColor_ == AIWZQ::Color::BLACK ? AIWZQ::Color::WHITE : AIWZQ::Color::BLACK);
        gameWindow_->endGame(winnerColor, QStringLiteral("win"),
                             (uid == userInfo_.uid) ? QStringLiteral("你赢了！") : QStringLiteral("你输了"));
    }
}

void GameController::dispatchUndoResp(const QJsonObject& data)
{
    const bool accepted = data.value(QStringLiteral("accepted")).toBool();
    if (accepted) {
        board_.undo(); board_.undo();
        gameWindow_->removeLastStone();
        gameWindow_->removeLastStone();
    } else {
        gameWindow_->showMessage(QStringLiteral("对方拒绝悔棋"));
    }
}

void GameController::dispatchDrawResp(const QJsonObject& data)
{
    const bool accepted = data.value(QStringLiteral("accepted")).toBool();
    if (accepted) gameWindow_->endGame(0, QStringLiteral("draw"), QStringLiteral("和棋"));
    else gameWindow_->showMessage(QStringLiteral("对方拒绝和棋"));
}

void GameController::dispatchGameOverNotify(const QJsonObject& data)
{
    const int winnerUid = static_cast<int>(data.value(QStringLiteral("winner_uid")).toDouble());
    const QString reason = data.value(QStringLiteral("reason")).toString();
    int winnerColor = 0;
    QString detail;
    if (reason == QStringLiteral("draw")) { detail = QStringLiteral("和棋"); }
    else {
        winnerColor = (winnerUid == userInfo_.uid) ? myColor_
                    : (myColor_ == AIWZQ::Color::BLACK ? AIWZQ::Color::WHITE : AIWZQ::Color::BLACK);
        detail = (winnerUid == userInfo_.uid) ? QStringLiteral("你赢了！") : QStringLiteral("你输了");
    }
    gameWindow_->endGame(winnerColor, reason, detail);
    // 不再立即隐藏对局窗口：先让棋盘上的结果覆盖层停留 2.5 秒，
    // 给爸爸时间看清"胜利/鼓励"，再延时切回大厅。
    setState(InLobby);
    QTimer::singleShot(2500, this, [this]() {
        if (gameWindow_) gameWindow_->hide();
        centerAndShow(mainWindow_);
        requestRoomList();
    });
}

void GameController::dispatchTimeoutNotify(const QJsonObject& data)
{
    const int timeoutUid = static_cast<int>(data.value(QStringLiteral("timeout_uid")).toDouble());
    const int winnerUid  = static_cast<int>(data.value(QStringLiteral("winner_uid")).toDouble());
    const int winnerColor = (winnerUid == userInfo_.uid) ? myColor_
                          : (myColor_ == AIWZQ::Color::BLACK ? AIWZQ::Color::WHITE : AIWZQ::Color::BLACK);
    const QString detail = (timeoutUid == userInfo_.uid) ? QStringLiteral("你超时了") : QStringLiteral("对方超时");
    gameWindow_->endGame(winnerColor, QStringLiteral("timeout"), detail);
}

void GameController::dispatchShopListResp(const QJsonObject& data)
{
    setState(InShop);
    QVector<ShopItemInfo> items;
    const QJsonArray arr = data.value(QStringLiteral("items")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        ShopItemInfo e;
        e.id        = static_cast<qint64>(o.value(QStringLiteral("id")).toDouble());
        e.name      = o.value(QStringLiteral("name")).toString();
        e.description = o.value(QStringLiteral("description")).toString();
        e.type      = o.value(QStringLiteral("type")).toString();
        e.price     = static_cast<qint64>(o.value(QStringLiteral("price")).toDouble());
        e.previewImage = o.value(QStringLiteral("preview_image")).toString();
        e.owned     = o.value(QStringLiteral("owned")).toBool();
        e.equipped  = o.value(QStringLiteral("equipped")).toBool();
        items.append(e);
    }
    shopWindow_->setItems(items);
    shopWindow_->setCoins(userInfo_.coins);
    centerAndShow(shopWindow_);
}

void GameController::dispatchBuyItemResp(const QJsonObject& data)
{
    const int code = data.value(QStringLiteral("code")).toInt();
    send(AIWZQ::MsgType::SHOP_LIST_REQ, QJsonObject());   // 购买后刷新商品/金币
    emit statusMessage(code == AIWZQ::ErrCode::OK ? QStringLiteral("购买成功")
                                                  : data.value(QStringLiteral("message")).toString());
}

void GameController::dispatchEquipItemResp(const QJsonObject& data)
{
    Q_UNUSED(data);
    send(AIWZQ::MsgType::SHOP_LIST_REQ, QJsonObject());   // 装备后刷新
}

void GameController::dispatchReconnectResp(const QJsonObject& data)
{
    reconnectPending_ = false;
    const int code = data.value(QStringLiteral("code")).toInt();
    if (code == AIWZQ::ErrCode::OK) {
        if (data.contains(QStringLiteral("user_info")))
            parseUserInfo(data.value(QStringLiteral("user_info")).toObject());
        mainWindow_->setUserInfo(QString::fromStdString(userInfo_.username),
                                 userInfo_.eloScore, userInfo_.wins, userInfo_.coins);
        setState(InLobby);
        emit statusMessage(QStringLiteral("会话已恢复"));
        centerAndShow(mainWindow_);
        requestRoomList();
    } else {
        token_.clear();
        SecureStorage::deleteToken();
        setState(Connected);
        showLoginAndHideOthers();
        emit statusMessage(QStringLiteral("会话已过期，请重新登录"));
    }
}
