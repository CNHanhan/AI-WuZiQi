// AI-WuZiQi 服务端网络层 — TcpServer 实现
#include "TcpServer.h"
#include "TcpConnection.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstring>

#ifdef __linux__
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace AIWZQ {

static void setNonBlocking(int fd) {
#ifdef __linux__
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
}

TcpServer::TcpServer(int port, int ioThreads)
    : port_(port), ioThreadCount_(std::max(1, ioThreads)),
      listenFd_(-1), running_(false), handler_(nullptr),
      nextConnId_(1), tlsEnabled_(false), sslCtx_(nullptr) {}

TcpServer::~TcpServer() {
    stop();
}

bool TcpServer::enableTls(const std::string& certFile, const std::string& keyFile) {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();
    sslCtx_ = SSL_CTX_new(TLS_server_method());
    if (!sslCtx_) {
        SPDLOG_ERROR("SSL_CTX_new failed");
        return false;
    }
    SSL_CTX_set_min_proto_version(sslCtx_, TLS1_3_VERSION);
    SSL_CTX_set_max_proto_version(sslCtx_, TLS1_3_VERSION);
    if (SSL_CTX_use_certificate_file(sslCtx_, certFile.c_str(), SSL_FILETYPE_PEM) <= 0) {
        SPDLOG_ERROR("load cert failed: {}", certFile);
        return false;
    }
    if (SSL_CTX_use_PrivateKey_file(sslCtx_, keyFile.c_str(), SSL_FILETYPE_PEM) <= 0) {
        SPDLOG_ERROR("load key failed: {}", keyFile);
        return false;
    }
    if (!SSL_CTX_check_private_key(sslCtx_)) {
        SPDLOG_ERROR("private key does not match cert");
        return false;
    }
    tlsEnabled_ = true;
    SPDLOG_INFO("TLS 1.3 enabled (cert={}, key={})", certFile, keyFile);
    return true;
}

int TcpServer::createAndListen(int port) {
#ifdef __linux__
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        SPDLOG_ERROR("socket failed, errno={}", errno);
        return -1;
    }
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        SPDLOG_ERROR("bind port={} failed, errno={}", port, errno);
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 4096) < 0) {
        SPDLOG_ERROR("listen failed, errno={}", errno);
        ::close(fd);
        return -1;
    }
    return fd;
#else
    (void)port;
    return -1;
#endif
}

void TcpServer::closeListen() {
#ifdef __linux__
    if (listenFd_ >= 0) { ::close(listenFd_); listenFd_ = -1; }
#endif
}

void TcpServer::start() {
    if (running_.load()) return;
    listenFd_ = createAndListen(port_);
    if (listenFd_ < 0) {
        SPDLOG_ERROR("server start failed: cannot listen on {}", port_);
        return;
    }
    running_.store(true);

    // IO EventLoop 与线程
    for (int i = 0; i < ioThreadCount_; ++i) {
        loops_.emplace_back(new EventLoop());
        int idx = i;
        ioThreads_.emplace_back([this, idx]() { ioWorker(idx); });
    }
    // accept 线程
    acceptThread_ = std::thread([this]() { acceptLoop(); });
    // timer 线程
    timerThread_ = std::thread([this]() { timerLoop(); });

    SPDLOG_INFO("TcpServer started on port {} (ioThreads={}, tls={})",
                port_, ioThreadCount_, tlsEnabled_);
}

void TcpServer::ioWorker(int index) {
    if (index >= 0 && index < static_cast<int>(loops_.size())) {
        loops_[index]->run();
    }
}

void TcpServer::acceptLoop() {
#ifdef __linux__
    while (running_.load()) {
        struct sockaddr_in peer;
        socklen_t len = sizeof(peer);
        int fd = ::accept4(listenFd_, reinterpret_cast<struct sockaddr*>(&peer),
                           &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            int e = errno;
            if (e == EINTR) continue;
            if (e == EAGAIN || e == EWOULDBLOCK) {
                ::usleep(1000);   // ET 监听下避免忙等；1ms 粒度足够
                continue;
            }
            SPDLOG_WARN("accept4 failed, errno={}", e);
            continue;
        }
        char ip[INET_ADDRSTRLEN] = "0.0.0.0";
        ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        int64_t cid = nextConnId_.fetch_add(1);
        size_t idx = static_cast<size_t>(cid) % loops_.size();
        EventLoop* loop = loops_[idx].get();

        auto conn = std::make_shared<net::TcpConnection>(
            fd, std::string(ip), cid, loop, handler_, tlsEnabled_ ? sslCtx_ : nullptr);
        IMessageHandler* h = handler_;

        // 注册 epoll 与 onConnected 都在 loop 线程内完成，保证事件顺序与线程归属一致
        loop->runInLoop([loop, fd, conn, h]() {
            loop->addConnection(fd, conn);
            if (h) h->onConnected(conn.get());
        });

        SPDLOG_DEBUG("accepted fd={} ip={} -> loop {} id={}", fd, ip, idx, cid);
    }
#endif
}

void TcpServer::timerLoop() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!running_.load()) break;
        if (onTimer_) {
            try {
                onTimer_();
            } catch (const std::exception& e) {
                SPDLOG_ERROR("onTimer exception: {}", e.what());
            }
        }
    }
}

void TcpServer::run() {
    for (auto& t : ioThreads_)   if (t.joinable()) t.join();
    if (acceptThread_.joinable()) acceptThread_.join();
    if (timerThread_.joinable())  timerThread_.join();
}

void TcpServer::stop() {
    if (!running_.exchange(false)) {
        // 即便未启动也确保清理 TLS 上下文（重复 stop 安全）
        if (sslCtx_) { SSL_CTX_free(sslCtx_); sslCtx_ = nullptr; }
        return;
    }
    closeListen();
    for (auto& loop : loops_) loop->stop();
    run();
    loops_.clear();
    if (sslCtx_) { SSL_CTX_free(sslCtx_); sslCtx_ = nullptr; }
    SPDLOG_INFO("TcpServer stopped");
}

} // namespace AIWZQ
