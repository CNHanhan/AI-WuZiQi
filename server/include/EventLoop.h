#pragma once

// AI-WuZiQi 服务端网络层 — IO 事件循环
// 每个 IO 线程独占一个 EventLoop（一个 epoll 实例）。
// 连接对象由本 EventLoop 以 shared_ptr 持有，所有连接事件都在 loop 线程内分发，
// 因此 TcpConnection 的输入缓冲无需加锁；输出缓冲因 biz 线程可能跨线程 send 而单独加锁。

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace AIWZQ {

class TcpConnection;

class EventLoop {
public:
    EventLoop();
    ~EventLoop();   // 定义于 .cpp，保证 TcpConnection 完整类型可见

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void run();             // 阻塞运行 epoll_wait 循环
    void stop();            // 置停止标志并唤醒

    int epollFd() const { return epollFd_; }

    // 线程安全：内部通过 runInLoop 投递到 loop 线程执行
    void addConnection(int fd, const std::shared_ptr<TcpConnection>& conn);
    void removeConnection(int fd);
    void enableWrite(int fd);
    void disableWrite(int fd);

    // 任意线程可调用；非 loop 线程时入队并唤醒
    void runInLoop(std::function<void()> fn);
    bool isLoopThread() const;
    void wakeup();

private:
    void executeTasks();
    void updateEvents(int fd, uint32_t events);   // 仅 loop 线程调用
    void epollAdd(int fd, uint32_t events);
    void epollDel(int fd);

    int  epollFd_;
    int  wakeupFd_;
    std::atomic<bool> stopping_;
    std::thread::id loopThreadId_;
    std::mutex taskMutex_;
    std::vector<std::function<void()>> pendingTasks_;
    std::unordered_map<int, std::shared_ptr<TcpConnection>> connections_;
};

} // namespace AIWZQ
