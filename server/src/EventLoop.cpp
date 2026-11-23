// AI-WuZiQi 服务端网络层 — EventLoop 实现（Linux epoll ET）
#include "EventLoop.h"
#include "TcpConnection.h"

#ifdef __linux__
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>
#endif

#ifdef _WIN32
// winsock2.h 必须先于 windows.h（spdlog 可能间接引入 windows.h）
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <spdlog/spdlog.h>

#include <chrono>
#include <utility>

namespace AIWZQ {

EventLoop::EventLoop()
    : epollFd_(-1), wakeupFd_(-1), stopping_(false) {
#ifdef __linux__
    epollFd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epollFd_ < 0) {
        SPDLOG_ERROR("epoll_create1 failed, errno={}", errno);
    }
    wakeupFd_ = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wakeupFd_ < 0) {
        SPDLOG_ERROR("eventfd failed, errno={}", errno);
    }
    if (epollFd_ >= 0 && wakeupFd_ >= 0) {
        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = wakeupFd_;
        ::epoll_ctl(epollFd_, EPOLL_CTL_ADD, wakeupFd_, &ev);
    }
#endif
}

EventLoop::~EventLoop() {
    stopping_.store(true);
#ifdef __linux__
    if (wakeupFd_ >= 0) ::close(wakeupFd_);
    if (epollFd_ >= 0) ::close(epollFd_);
#endif
}

void EventLoop::run() {
#ifdef __linux__
    loopThreadId_ = std::this_thread::get_id();
    std::vector<struct epoll_event> events(1024);
    while (!stopping_.load()) {
        int n = ::epoll_wait(epollFd_, events.data(),
                             static_cast<int>(events.size()), 1000);
        if (n < 0) {
            if (errno == EINTR) continue;
            SPDLOG_ERROR("epoll_wait failed, errno={}", errno);
            break;
        }
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t e = events[i].events;
            if (fd == wakeupFd_) {
                uint64_t one = 0;
                (void)::read(wakeupFd_, &one, sizeof(one));
                executeTasks();
                continue;
            }
            auto it = connections_.find(fd);
            if (it == connections_.end()) continue;
            auto conn = it->second;
            if (!conn || !conn->connected()) continue;

            if ((e & EPOLLRDHUP) || (e & EPOLLHUP) || (e & EPOLLERR)) {
                conn->handleClose();
                continue;
            }
            if (e & EPOLLIN)  conn->handleRead();
            if (!conn->connected()) continue;
            if (e & EPOLLOUT) conn->handleWrite();
        }
        // 直连读空后可能仍有投递任务（如跨线程 close），这里再排空一次
        executeTasks();
    }
#elif defined(_WIN32)
    // Windows select 方案：无法监听 eventfd，改为 100ms 超时轮询，每轮排空 pendingTasks_。
    // 连接注册完全靠 connections_ 映射（epollAdd/Del/updateEvents 在 Windows 下为空）。
    loopThreadId_ = std::this_thread::get_id();
    timeval tv;
    while (!stopping_.load()) {
        executeTasks();

        fd_set readfds;
        fd_set writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        int maxFd = 0;

        // 先做一次快照遍历，构造读/写集合（map 在 loop 线程内，任务已排空，迭代安全）
        for (auto& kv : connections_) {
            int fd = kv.first;
            auto& conn = kv.second;
            if (!conn || !conn->connected()) continue;
            SOCKET s = static_cast<SOCKET>(fd);
            FD_SET(s, &readfds);
            if (conn->wantWrite()) FD_SET(s, &writefds);
            if (static_cast<int>(s) > maxFd) maxFd = static_cast<int>(s);
        }

        tv.tv_sec = 0;
        tv.tv_usec = 100000;   // 100ms：兼作任务唤醒轮询粒度

        // Winsock 的 select() 当三个集合全空时会立即返回 SOCKET_ERROR(WSAEINVAL)，
        // 且空集合无意义。此时退化为 sleep 100ms 后仅排任务，避免误判退出。
        int n;
        if (maxFd == 0 && readfds.fd_count == 0 && writefds.fd_count == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            executeTasks();
            continue;
        }
        n = ::select(0, &readfds, &writefds, NULL, &tv);
        if (n < 0) {
            int err = ::WSAGetLastError();
            if (err == WSAEINTR) continue;
            SPDLOG_ERROR("select failed, err={}", err);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;   // 不 break：loop 线程必须存活以处理后续连接
        }
        if (n == 0) {
            executeTasks();    // 超时：仅排任务（顶部也排一次，覆盖 select 期间入队）
            continue;
        }

        // 遍历读集：先读。handleRead/handleClose 可能通过 removeConnection 修改 connections_，
        // 但 removeConnection 走 runInLoop；当前已在 loop 线程，会同步执行 erase。
        // 因此用 fd 列表快照遍历，并对每个 fd 重新查表，避免迭代器失效。
        std::vector<int> readyRead;
        std::vector<int> readyWrite;
        readyRead.reserve(connections_.size());
        readyWrite.reserve(connections_.size());
        for (auto& kv : connections_) {
            int fd = kv.first;
            SOCKET s = static_cast<SOCKET>(fd);
            if (FD_ISSET(s, &readfds))  readyRead.push_back(fd);
            if (FD_ISSET(s, &writefds)) readyWrite.push_back(fd);
        }
        for (int fd : readyRead) {
            auto it = connections_.find(fd);
            if (it == connections_.end()) continue;
            auto conn = it->second;
            if (!conn || !conn->connected()) continue;
            conn->handleRead();
        }
        for (int fd : readyWrite) {
            auto it = connections_.find(fd);
            if (it == connections_.end()) continue;
            auto conn = it->second;
            if (!conn || !conn->connected()) continue;
            conn->handleWrite();
        }
        executeTasks();   // 直连读空后可能仍有投递任务，再排空一次
    }
#else
    while (!stopping_.load()) {
        executeTasks();
        break;   // 其他非 Windows/非 Linux 占位：立即退出避免空转
    }
#endif
}

void EventLoop::stop() {
    stopping_.store(true);
    wakeup();
}

void EventLoop::wakeup() {
#ifdef __linux__
    if (wakeupFd_ >= 0) {
        uint64_t one = 1;
        ssize_t r = ::write(wakeupFd_, &one, sizeof(one));
        (void)r;
    }
#endif
    // Windows：select 用 100ms 超时轮询排空 pendingTasks_，无需显式唤醒。
}

bool EventLoop::isLoopThread() const {
    return std::this_thread::get_id() == loopThreadId_;
}

void EventLoop::runInLoop(std::function<void()> fn) {
    if (isLoopThread()) {
        fn();
        return;
    }
    {
        std::lock_guard<std::mutex> lk(taskMutex_);
        pendingTasks_.push_back(std::move(fn));
    }
    wakeup();
}

void EventLoop::executeTasks() {
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lk(taskMutex_);
        if (pendingTasks_.empty()) return;
        tasks.swap(pendingTasks_);
    }
    for (auto& t : tasks) t();
}

void EventLoop::epollAdd(int fd, uint32_t events) {
#ifdef __linux__
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    if (::epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        SPDLOG_WARN("epoll_ctl ADD fd={} errno={}", fd, errno);
    }
#endif
}

void EventLoop::epollDel(int fd) {
#ifdef __linux__
    struct epoll_event ev;
    ::epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &ev);
#endif
}

void EventLoop::updateEvents(int fd, uint32_t events) {
#ifdef __linux__
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    if (::epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
        SPDLOG_WARN("epoll_ctl MOD fd={} errno={}", fd, errno);
    }
#endif
}

void EventLoop::addConnection(int fd, const std::shared_ptr<TcpConnection>& conn) {
    auto self = conn;
    runInLoop([this, fd, self]() {
#ifdef __linux__
        connections_[fd] = self;
        uint32_t events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        if (self->initialWantWrite()) events |= EPOLLOUT;
        epollAdd(fd, events);
#else
        // Windows：select 每轮按 connections_ 重建 fd_set，注册即入映射；wantWrite 由 conn 自报。
        (void)self;
        connections_[fd] = self;
#endif
    });
}

void EventLoop::removeConnection(int fd) {
    runInLoop([this, fd]() {
#ifdef __linux__
        epollDel(fd);
#endif
        connections_.erase(fd);
    });
}

void EventLoop::enableWrite(int fd) {
    runInLoop([this, fd]() {
#ifdef __linux__
        updateEvents(fd, EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLOUT);
#else
        // Windows：select 写集由 conn->wantWrite() 动态决定；writing_ 已在 TcpConnection 内置位。
        (void)fd;
#endif
    });
}

void EventLoop::disableWrite(int fd) {
    runInLoop([this, fd]() {
#ifdef __linux__
        updateEvents(fd, EPOLLIN | EPOLLET | EPOLLRDHUP);
#else
        (void)fd;
#endif
    });
}

} // namespace AIWZQ
