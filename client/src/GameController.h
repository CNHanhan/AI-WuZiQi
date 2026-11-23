#pragma once

// GameController：客户端核心协调器 / 全流程状态机。
//  - 持有所有窗口指针 + TcpClient + AIManager + 本地 GomokuBoard + UserInfo
//  - 窗口信号 -> 网络请求；messageReceived -> 分派直接调用窗口 public 回填方法
//  - 登录/大厅/房间/对局/商城/排行榜/离线AI 全流程；窗口创建与切换亦在此
//  - 断线重连 reconnect_req(token, last_seq)

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QSet>
#include <QAbstractSocket>

#include "Types.h"
#include "GomokuBoard.h"
#include "ProtocolCodec.h"

class TcpClient;
class AIManager;
class CryptoManager;
class SecureStorage;
class QTimer;

class LoginWindow;
class MainWindow;
class RoomWindow;
class GameWindow;
class ShopWindow;
class RankDialog;

class GameController : public QObject
{
    Q_OBJECT
public:
    enum State {
        Offline = 0, Connecting, Connected, LoggedIn, InLobby,
        InRoom, InGame, InShop, InOfflineGame
    };
    Q_ENUM(State)

    explicit GameController(QObject* parent = nullptr);
    ~GameController() override;

    // 窗口创建（controller 负责 show/hide 切换）
    void createWindows();
    void showAllWindows();

    State state() const { return state_; }
    QString token() const { return token_; }
    AIWZQ::UserInfo userInfo() const { return userInfo_; }
    void setSessionKey(const QByteArray& key32) { sessionKey_ = key32; }
    void setServer(const QString& host, quint16 port);
    void start();                          // 启动：连接服务器

public slots:
    // 登录
    void onLoginRequested(const QString& username, const QString& password);
    void onRegisterRequested(const QString& username, const QString& password, const QString& email);
    void onFindPwdRequested(const QString& email);
    void onVerifyCodeRequested(const QString& email);
    void onResetPwdRequested(const QString& email, const QString& code, const QString& newPassword);
    void onOfflineGameRequested();

    // 大厅
    void onRefreshRoomsRequested(int page, int pageSize);
    void onCreateRoomRequested(int totalTime, int stepTime, const QString& password);
    void onJoinRoomRequested(qint64 roomId, const QString& password);
    void onRandomMatchRequested();
    void onChatRequested(const QString& content);
    void onRankRequested();                       // MainWindow 入口：打开排行榜
    void onRankQueryRequested(const QString& rankType, int page); // RankDialog 请求
    void onShopRequested();
    void onLogoutRequested();

    // 房间
    void onReadyRequested(bool ready);
    void onLeaveRoomRequested();
    void onRoomWaitTimedOut();     // RoomWindow 等待对手 150 秒超时
    void onMatchWaitTimedOut();    // 随机匹配 30 秒超时
    void onMatchPollTick();        // 随机匹配等待期间每 2 秒重发一次检查配对结果

    // 对局
    void onPlaceStoneRequested(int x, int y);
    void onUndoRequested();
    void onDrawRequested();
    void onSurrenderRequested();
    void onGameReadyRequested(bool ready);
    void onLeaveGameRequested();

    // 商城
    void onBuyItemRequested(qint64 itemId);
    void onEquipItemRequested(qint64 itemId);
    void onRequestShopList(const QString& type = QString());
    void onRechargeRequested(qint64 amount);   // 充值：弹确认框，选"是"本地加金币

    // 网络
    void onMessageReceived(const QJsonObject& msg);
    void onConnected();
    void onDisconnected();
    void onSocketError(QAbstractSocket::SocketError err);

    // 离线 AI
    void onAiMoveReady(int x, int y);
    void onOfflineGameOver(int result);

signals:
    void stateChanged(State state);
    void statusMessage(const QString& text);

private:
    void send(const QString& type, const QJsonObject& data = QJsonObject());
    void sendSensitive(const QString& type, QJsonObject data);
    void updateLastSeq(qint64 seq);
    void setState(State s);

    void dispatchLoginResp(const QJsonObject& data);
    void dispatchRegisterResp(const QJsonObject& data);
    void dispatchFindPwdResp(const QJsonObject& data);
    void dispatchRoomListResp(const QJsonObject& data);
    void dispatchPlayerListResp(const QJsonObject& data);
    void dispatchRankListResp(const QJsonObject& data);
    void dispatchHallChatNotify(const QJsonObject& data);
    void dispatchCreateRoomResp(const QJsonObject& data);
    void dispatchJoinRoomResp(const QJsonObject& data);
    void dispatchRoomInfoResp(const QJsonObject& data);
    void dispatchReadyResp(const QJsonObject& data);
    void dispatchGameStartNotify(const QJsonObject& data);
    void dispatchPlaceStoneNotify(const QJsonObject& data);
    void dispatchUndoResp(const QJsonObject& data);
    void dispatchDrawResp(const QJsonObject& data);
    void dispatchGameOverNotify(const QJsonObject& data);
    void dispatchTimeoutNotify(const QJsonObject& data);
    void dispatchShopListResp(const QJsonObject& data);
    void dispatchBuyItemResp(const QJsonObject& data);
    void dispatchEquipItemResp(const QJsonObject& data);
    void dispatchReconnectResp(const QJsonObject& data);

    void parseUserInfo(const QJsonObject& o);
    void requestRoomList(int page = 1, int pageSize = 20);
    void doReconnect();
    void showLoginAndHideOthers();

private:
    State state_ = Offline;
    QString host_ = QStringLiteral("127.0.0.1");
    quint16 port_ = 8889;

    QString token_;
    qint64  lastSeq_ = 0;
    AIWZQ::UserInfo userInfo_;

    AIWZQ::GomokuBoard board_;
    int myColor_ = AIWZQ::Color::BLACK;
    int blackLeft_ = 180;
    int whiteLeft_ = 180;
    int stepLeft_ = 30;
    int offlineDifficulty_ = 2;

    QByteArray sessionKey_;
    bool reconnectPending_ = false;
    QSet<QString> seenNonces_;

    QTimer* matchWaitTimer_ = nullptr;   // 随机匹配 30 秒总超时
    QTimer* matchPollTimer_ = nullptr;   // 随机匹配等待期间的 2 秒轮询定时器

    TcpClient*     tcp_    = nullptr;
    AIManager*     ai_     = nullptr;
    CryptoManager* crypto_ = nullptr;
    ProtocolCodec* codec_  = nullptr;

    LoginWindow* loginWindow_ = nullptr;
    MainWindow*  mainWindow_  = nullptr;
    RoomWindow*  roomWindow_  = nullptr;
    GameWindow*  gameWindow_  = nullptr;
    ShopWindow*  shopWindow_  = nullptr;
    RankDialog*  rankDialog_  = nullptr;
};
