// AI-WuZiQi 服务端网络层 — EventLoop 实现（Linux epoll ET）
#include "EventLoop.h"
#include "TcpConnection.h"

#ifdef __linux__
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <cerrno>
#endif

#include <spdlog/spdlog.h>

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
                ::read(wakeupFd_, &one, sizeof(one));
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
#else
    while (!stopping_.load()) {
        executeTasks();
        break;   // 非 Linux 占位：立即退出避免空转
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
        connections_[fd] = self;
        uint32_t events = EPOLLIN | EPOLLET | EPOLLRDHUP;
        if (self->initialWantWrite()) events |= EPOLLOUT;
        epollAdd(fd, events);
    });
}

void EventLoop::removeConnection(int fd) {
    runInLoop([this, fd]() {
        epollDel(fd);
        connections_.erase(fd);
    });
}

void EventLoop::enableWrite(int fd) {
    runInLoop([this, fd]() {
        updateEvents(fd, EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLOUT);
    });
}

void EventLoop::disableWrite(int fd) {
    runInLoop([this, fd]() {
        updateEvents(fd, EPOLLIN | EPOLLET | EPOLLRDHUP);
    });
}

} // namespace AIWZQ
