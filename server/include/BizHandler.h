#pragma once
/**
 * BizHandler.h — 业务消息处理器（网络层 ↔ 业务层唯一入口）
 * 对应设计文档：01-服务端设计.md 2.3~2.8；03-通信协议设计.md 4.1~4.4/五
 *               common/include/IMessageHandler.h（冻结契约）
 *
 * 与 net 的唯一契约：
 *   extern "C" IMessageHandler* createBizHandler();  // 定义在 BizHandler.cpp
 */
#include "IMessageHandler.h"

#include "Database.h"
#include "RateLimiter.h"
#include "RedisClient.h"
#include "RoomManager.h"
#include "SecurityLogger.h"
#include "SessionManager.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace AIWZQ {

class BizHandler : public IMessageHandler {
public:
    BizHandler();
    ~BizHandler() override;

    void onConnected(IConnection* conn) override;
    void onDisconnected(IConnection* conn) override;
    void onMessage(IConnection* conn, const Message& msg) override;

    // 由主循环周期性调用：扫描超时房间并广播结束通知
    void tick();

private:
    // ---- 用户系统 ----
    void handleRegister(IConnection* c, const Message& m);
    void handleLogin(IConnection* c, const Message& m);
    void handleLogout(IConnection* c, const Message& m);
    void handleFindPwd(IConnection* c, const Message& m);
    void handleChangePwd(IConnection* c, const Message& m);

    // ---- 大厅 ----
    void handleRoomList(IConnection* c, const Message& m);
    void handlePlayerList(IConnection* c, const Message& m);
    void handleHallChat(IConnection* c, const Message& m);
    void handleRankList(IConnection* c, const Message& m);

    // ---- 房间/对局 ----
    void handleCreateRoom(IConnection* c, const Message& m);
    void handleJoinRoom(IConnection* c, const Message& m);
    void handleRandomJoin(IConnection* c, const Message& m);
    void handleReady(IConnection* c, const Message& m);
    void handlePlaceStone(IConnection* c, const Message& m);
    void handleUndo(IConnection* c, const Message& m);
    void handleDraw(IConnection* c, const Message& m);
    void handleSurrender(IConnection* c, const Message& m);

    // ---- 商城 ----
    void handleShopList(IConnection* c, const Message& m);
    void handleBuyItem(IConnection* c, const Message& m);
    void handleEquipItem(IConnection* c, const Message& m);

    // ---- 心跳/重连 ----
    void handleHeartbeat(IConnection* c, const Message& m);
    void handleReconnect(IConnection* c, const Message& m);

    // ---- 工具 ----
    void reply(IConnection* c, const Message& m, const std::string& type,
               const json& data);
    void replyError(IConnection* c, const Message& m, const std::string& type,
                    int code, const std::string& message);
    // 校验登录态；失败自动回 TOKEN_INVALID 响应，返回 false
    bool requireAuth(IConnection* c, const Message& m, const std::string& respType, int64_t& uid);
    // 广播给房间内双方
    void broadcastRoom(int64_t roomId, const std::string& type, const json& data);
    // 向指定玩家发送 game_start_notify
    void sendGameStart_(IConnection* c, int64_t uid, int64_t roomId);

    Database       db_;
    RedisClient    redis_;
    SessionManager sessions_;
    RateLimiter    limiter_;
    SecurityLogger security_;
    RoomManager    rooms_;

    // 已登录连接：connId -> {uid, token}
    struct ConnState { int64_t uid = 0; std::string token; };
    std::map<int64_t, ConnState> connStates_;
    mutable std::mutex connMtx_;

    // 大厅在线连接集合（用于广播聊天）
    std::vector<IConnection*> hallConns_;
    mutable std::mutex hallMtx_;

    // 房间连接映射：uid -> conn（用于房间内广播）
    std::map<int64_t, IConnection*> uidConns_;
    mutable std::mutex uidMtx_;

    // 找回密码 token -> uid（有效期 30 分钟，存 Redis）
    static constexpr int PWD_RESET_TTL = 1800;

    // 后台超时扫描线程（兜底；net 层 setOnTimer 也调用 tick()，两者幂等）
    std::thread tickThread_;
    std::atomic<bool> tickStop_{false};
};

} // namespace AIWZQ

// 网络层 main.cpp 通过此工厂取得处理器实例（唯一对外契约）
extern "C" AIWZQ::IMessageHandler* createBizHandler();
