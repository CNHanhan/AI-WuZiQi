// AI-WuZiQi 服务端网络层 — TcpServer 实现
#include "TcpServer.h"
#include "TcpConnection.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
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

#ifdef _WIN32
// winsock2.h 必须先于 windows.h；TcpServer.h 已经通过 TcpConnection.h 先行引入，
// 这里再显式包含一次保证自洽（重复包含有 include guard 保护）。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace AIWZQ {

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
#elif defined(_WIN32)
    WSADATA wsaData;
    int wsaRet = ::WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaRet != 0) {
        SPDLOG_ERROR("WSAStartup failed, ret={}", wsaRet);
        return -1;
    }
    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        SPDLOG_ERROR("socket failed, err={}", ::WSAGetLastError());
        return -1;
    }
    int one = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&one), sizeof(one));
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        SPDLOG_ERROR("bind port={} failed, err={}", port, ::WSAGetLastError());
        ::closesocket(sock);
        return -1;
    }
    if (::listen(sock, SOMAXCONN) == SOCKET_ERROR) {
        SPDLOG_ERROR("listen failed, err={}", ::WSAGetLastError());
        ::closesocket(sock);
        return -1;
    }
    u_long nonBlocking = 1;   // FIONBIO：非阻塞监听，acceptLoop 轮询
    if (::ioctlsocket(sock, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
        SPDLOG_ERROR("ioctlsocket FIONBIO failed, err={}", ::WSAGetLastError());
        ::closesocket(sock);
        return -1;
    }
    return static_cast<int>(sock);
#else
    (void)port;
    return -1;
#endif
}

void TcpServer::closeListen() {
#ifdef __linux__
    if (listenFd_ >= 0) { ::close(listenFd_); listenFd_ = -1; }
#elif defined(_WIN32)
    if (listenFd_ >= 0) {
        ::closesocket(static_cast<SOCKET>(listenFd_));
        listenFd_ = -1;
    }
    ::WSACleanup();
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

        auto conn = std::make_shared<TcpConnection>(
            fd, std::string(ip), cid, loop, handler_, tlsEnabled_ ? sslCtx_ : nullptr);
        IMessageHandler* h = handler_;

        // 注册 epoll 与 onConnected 都在 loop 线程内完成，保证事件顺序与线程归属一致
        loop->runInLoop([loop, fd, conn, h]() {
            loop->addConnection(fd, conn);
            if (h) h->onConnected(conn.get());
        });

        SPDLOG_DEBUG("accepted fd={} ip={} -> loop {} id={}", fd, ip, idx, cid);
    }
#elif defined(_WIN32)
    while (running_.load()) {
        struct sockaddr_in peer;
        int len = static_cast<int>(sizeof(peer));
        SOCKET client = ::accept(static_cast<SOCKET>(listenFd_),
                                 reinterpret_cast<struct sockaddr*>(&peer), &len);
        if (client == INVALID_SOCKET) {
            int e = ::WSAGetLastError();
            if (e == WSAEINTR) continue;
            if (e == WSAEWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));   // 非阻塞 accept：1ms 退避
                continue;
            }
            SPDLOG_WARN("accept failed, err={}", e);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        u_long nonBlocking = 1;
        if (::ioctlsocket(client, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
            SPDLOG_WARN("ioctlsocket(client) failed, err={}", ::WSAGetLastError());
            ::closesocket(client);
            continue;
        }
        char ip[INET_ADDRSTRLEN] = "0.0.0.0";
        unsigned long peerAddr = peer.sin_addr.S_un.S_addr;
        const char* inet = ::inet_ntoa(peer.sin_addr);
        if (inet) std::snprintf(ip, sizeof(ip), "%s", inet);
        (void)peerAddr;
        int fd = static_cast<int>(client);
        int64_t cid = nextConnId_.fetch_add(1);
        size_t idx = static_cast<size_t>(cid) % loops_.size();
        EventLoop* loop = loops_[idx].get();

        auto conn = std::make_shared<TcpConnection>(
            fd, std::string(ip), cid, loop, handler_, tlsEnabled_ ? sslCtx_ : nullptr);
        IMessageHandler* h = handler_;

        // 注册连接与 onConnected 都在 loop 线程内完成，保证事件顺序与线程归属一致
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
