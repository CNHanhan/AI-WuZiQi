#pragma once
#include "Protocol.h"
#include <cstdint>
#include <string>

namespace AIWZQ {

// ============ 网络层 ↔ 业务层 解耦契约（由 lead 冻结，双方遵守） ============
// net 模块实现 IConnection（TcpConnection 继承它）
// biz 模块实现 IMessageHandler（BizHandler 继承它）
// main.cpp 负责把两者装配：server.setHandler(&biz); server.run();

// 业务层看到的一条连接（网络层提供）
class IConnection {
public:
    virtual ~IConnection() = default;
    virtual int64_t id() const = 0;                    // 连接唯一 id
    virtual void send(const Message& msg) = 0;         // 向该连接发送一条消息（异步）
    virtual void close() = 0;                          // 主动断开
    virtual std::string peerIp() const = 0;            // 对端 IP
    virtual int64_t uid() const = 0;                   // 登录后的 uid，未登录为 0
    virtual void setUid(int64_t uid) = 0;              // 登录成功后由 biz 设置
};

// 网络层回调给业务层的事件（业务层实现）
class IMessageHandler {
public:
    virtual ~IMessageHandler() = default;
    virtual void onConnected(IConnection* conn) = 0;
    virtual void onDisconnected(IConnection* conn) = 0;
    virtual void onMessage(IConnection* conn, const Message& msg) = 0;
};

} // namespace AIWZQ
