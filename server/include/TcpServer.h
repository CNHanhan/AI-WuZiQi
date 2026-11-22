#pragma once

// AI-WuZiQi 服务端网络层 — Reactor 主入口
// 单 accept 线程 + 多 IO 线程（每线程一个 EventLoop）+ 单 timer 线程。
// 新连接轮转分配给 IO EventLoop；定时器每秒触发 onTimer 回调（默认空实现，
// 供 biz 模块注册超时轮询逻辑，保持 IMessageHandler 契约不变）。

#include "IMessageHandler.h"
#include "EventLoop.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

#include <openssl/ssl.h>

namespace AIWZQ {

class TcpServer {
public:
    explicit TcpServer(int port, int ioThreads = 4);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    void setHandler(IMessageHandler* handler) { handler_ = handler; }
    void setOnTimer(std::function<void()> cb) { onTimer_ = std::move(cb); }

    void start();   // 初始化监听 / 线程池 / 定时器线程，立即返回
    void stop();    // 停止所有线程并释放资源
    void run();     // start() 后阻塞等待（join 所有工作线程）

    // TLS 配置（可选；未调用则明文 8888）
    bool enableTls(const std::string& certFile, const std::string& keyFile);

private:
    void acceptLoop();
    void timerLoop();
    void ioWorker(int index);

    int  createAndListen(int port);
    void closeListen();

    int port_;
    int ioThreadCount_;
    int listenFd_;
    std::atomic<bool> running_;

    IMessageHandler* handler_;
    std::function<void()> onTimer_;

    // 每个 IO 线程一个 EventLoop（拥有其生命周期）
    std::vector<std::unique_ptr<EventLoop>> loops_;
    std::vector<std::thread> ioThreads_;
    std::thread acceptThread_;
    std::thread timerThread_;
    std::atomic<uint64_t> nextConnId_;

    // TLS
    bool tlsEnabled_;
    SSL_CTX* sslCtx_;
};

} // namespace AIWZQ
