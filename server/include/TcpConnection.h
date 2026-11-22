#pragma once

// AI-WuZiQi 服务端网络层 — 单条 TCP 连接
// 实现 IConnection 契约，负责帧解码（popFrame）、非阻塞发送、可选 TLS。
// 生命周期由所属 EventLoop 以 shared_ptr 持有；所有 IO 回调都在 loop 线程执行。

#include "IMessageHandler.h"
#include "Protocol.h"
#include "EventLoop.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <openssl/ssl.h>
#include <openssl/err.h>

#ifdef __linux__
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#define AIWZQ_CLOSE(fd)            ::close(fd)
#define AIWZQ_RECV(fd, buf, len)   ::recv((fd), (buf), (len), 0)
#define AIWZQ_SEND(fd, buf, len)   ::send((fd), (buf), (len), MSG_NOSIGNAL)
#define AIWZQ_ERRNO                errno
#else
// 非 Linux 平台编译期占位：网络层目标平台为 Linux(epoll)，此处仅提供可解析占位。
#pragma message("AI-WuZiQi server network layer targets Linux (epoll); non-Linux build uses stubs.")
#include <cerrno>
#define AIWZQ_CLOSE(fd)            (0)
#define AIWZQ_RECV(fd, buf, len)   (-1)
#define AIWZQ_SEND(fd, buf, len)   (-1)
#define AIWZQ_ERRNO                EIO
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace AIWZQ {

// 简单连续字节缓冲，支持帧弹出与消费；inBuf_/outBuf_ 通用。
// 注意：改名 NetBuffer 避免与 common/Protocol.h 中的 AIWZQ::Buffer 重名。
class NetBuffer {
public:
    void append(const char* p, size_t n) { if (n) data_.append(p, n); }
    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }
    const char* data() const { return data_.data(); }
    void consume(size_t n) { if (n >= data_.size()) data_.clear(); else data_.erase(0, n); }

    // 弹出一帧（半包返回 false，不消费）；非法帧抛 DecodeError。
    bool popFrame(Message& out) {
        size_t used = 0;
        if (!decodeOneFrame(data_.data(), data_.size(), out, used)) return false;
        consume(used);
        return true;
    }
private:
    std::string data_;
};

class TcpConnection : public IConnection,
                      public std::enable_shared_from_this<TcpConnection> {
public:
    TcpConnection(int fd, const std::string& peerIp, int64_t id,
                  EventLoop* loop, IMessageHandler* handler, SSL_CTX* sslCtx)
        : sockFd_(fd), writing_(false), connected_(true), uid_(0),
          id_(id), peerIp_(peerIp), loop_(loop), handler_(handler),
          ssl_(nullptr), handshaked_(sslCtx == nullptr) {
        if (sslCtx) {
            ssl_ = SSL_new(sslCtx);
            if (ssl_) SSL_set_fd(ssl_, fd);
            writing_ = true;   // 握手期间同时关注 EPOLLOUT
        }
    }

    ~TcpConnection() override {
        if (ssl_) { SSL_free(ssl_); ssl_ = nullptr; }
        if (sockFd_ >= 0) { AIWZQ_CLOSE(sockFd_); sockFd_ = -1; }
    }

    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    // ---------------- IConnection ----------------
    int64_t id() const override { return id_; }
    void send(const Message& msg) override {
        if (!connected_.load()) return;
        std::string frame = encodeFrame(msg);
        {
            std::lock_guard<std::mutex> lk(outMutex_);
            outBuf_.append(frame.data(), frame.size());
        }
        auto self = shared_from_this();
        loop_->runInLoop([this, self]() { sendInLoop(); (void)self; });
    }
    void close() override {
        if (!connected_.load()) return;
        auto self = shared_from_this();
        loop_->runInLoop([this, self]() { handleClose(); (void)self; });
    }
    std::string peerIp() const override { return peerIp_; }
    int64_t uid() const override { return uid_.load(); }
    void setUid(int64_t u) override { uid_.store(u); }

    // ---------------- 网络层内部（仅 loop 线程调用） ----------------
    int fd() const { return sockFd_; }
    bool connected() const { return connected_.load(); }
    bool initialWantWrite() const { return ssl_ != nullptr && !handshaked_; }

    void handleRead() {
        if (!connected_.load()) return;
        if (ssl_ && !handshaked_) {
            driveHandshake();
            if (!handshaked_) return;
        }
        char buf[65536];
        while (true) {
            ssize_t n = doRead(buf, sizeof(buf));
            if (n > 0) {
                inBuf_.append(buf, static_cast<size_t>(n));
                while (true) {
                    Message msg;
                    bool got = false;
                    try {
                        got = inBuf_.popFrame(msg);
                    } catch (const DecodeError&) {
                        handleClose();
                        return;
                    }
                    if (!got) break;
                    if (handler_) handler_->onMessage(this, msg);
                }
                continue;
            }
            if (n == 0) { handleClose(); return; }            // 对端关闭
            int e = AIWZQ_ERRNO;
            if (e == EINTR) continue;
            if (ssl_) {
                int se = SSL_get_error(ssl_, static_cast<int>(n));
                if (se == SSL_ERROR_WANT_READ || se == SSL_ERROR_WANT_WRITE) return;
                if (se == SSL_ERROR_ZERO_RETURN) { handleClose(); return; }
            } else if (e == EAGAIN || e == EWOULDBLOCK) {
                return;                                        // ET：读空
            }
            handleClose();
            return;
        }
    }

    void handleWrite() {
        if (!connected_.load()) return;
        if (ssl_ && !handshaked_) {
            driveHandshake();
            if (!handshaked_) return;
        }
        sendInLoop();
    }

    void handleClose() {
        auto self = shared_from_this();       // 保活到函数结束
        if (!connected_.exchange(false)) return;
        if (closeCb_) closeCb_();
        if (loop_) loop_->removeConnection(sockFd_);
        if (handler_) handler_->onDisconnected(this);
        if (ssl_) { SSL_free(ssl_); ssl_ = nullptr; }
        if (sockFd_ >= 0) { AIWZQ_CLOSE(sockFd_); sockFd_ = -1; }
    }

    void setCloseCallback(std::function<void()> cb) { closeCb_ = std::move(cb); }

private:
    void driveHandshake() {
        int r = SSL_accept(ssl_);
        if (r == 1) {
            handshaked_ = true;
            bool hasData;
            { std::lock_guard<std::mutex> lk(outMutex_); hasData = !outBuf_.empty(); }
            if (!hasData && writing_) { writing_ = false; loop_->disableWrite(sockFd_); }
            return;
        }
        int err = SSL_get_error(ssl_, r);
        if (err == SSL_ERROR_WANT_READ) {
            if (writing_) { writing_ = false; loop_->disableWrite(sockFd_); }
            return;
        }
        if (err == SSL_ERROR_WANT_WRITE) {
            if (!writing_) { writing_ = true; loop_->enableWrite(sockFd_); }
            return;
        }
        handleClose();   // 握手致命错误
    }

    ssize_t doRead(char* buf, size_t len) {
        if (ssl_) return SSL_read(ssl_, buf, static_cast<int>(len));
        return AIWZQ_RECV(sockFd_, buf, len);
    }
    ssize_t doWrite(const char* buf, size_t len) {
        if (ssl_) return SSL_write(ssl_, buf, static_cast<int>(len));
        return AIWZQ_SEND(sockFd_, buf, len);
    }

    void sendInLoop() {
        if (!connected_.load()) return;
        if (ssl_ && !handshaked_) return;   // 握手未完成时由 handleWrite 驱动

        std::string chunk;
        {
            std::lock_guard<std::mutex> lk(outMutex_);
            chunk.assign(outBuf_.data(), outBuf_.size());
        }
        if (chunk.empty()) {
            if (writing_) { writing_ = false; loop_->disableWrite(sockFd_); }
            return;
        }

        size_t sent = 0;
        while (sent < chunk.size()) {
            ssize_t n = doWrite(chunk.data() + sent, chunk.size() - sent);
            if (n > 0) { sent += static_cast<size_t>(n); continue; }
            int e = AIWZQ_ERRNO;
            if (n < 0 && e == EINTR) continue;
            if (n < 0 && ssl_) {
                int se = SSL_get_error(ssl_, static_cast<int>(n));
                if (se == SSL_ERROR_WANT_READ || se == SSL_ERROR_WANT_WRITE) break;
            } else if (n < 0 && (e == EAGAIN || e == EWOULDBLOCK)) {
                break;
            } else if (n == 0) {
                break;
            } else {
                handleClose();
                return;
            }
            break;
        }

        {
            std::lock_guard<std::mutex> lk(outMutex_);
            outBuf_.consume(sent);
        }
        bool hasMore;
        { std::lock_guard<std::mutex> lk(outMutex_); hasMore = !outBuf_.empty(); }
        if (hasMore) {
            if (!writing_) { writing_ = true; loop_->enableWrite(sockFd_); }
        } else if (writing_) {
            writing_ = false;
            loop_->disableWrite(sockFd_);
        }
    }

    int sockFd_;
    NetBuffer inBuf_;
    NetBuffer outBuf_;
    std::mutex outMutex_;
    bool writing_;                        // 仅 loop 线程访问
    std::atomic<bool> connected_;
    std::atomic<int64_t> uid_;
    int64_t id_;
    std::string peerIp_;
    EventLoop* loop_;
    IMessageHandler* handler_;
    std::function<void()> closeCb_;
    SSL* ssl_;
    bool handshaked_;
};

} // namespace AIWZQ
