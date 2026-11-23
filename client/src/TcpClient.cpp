#include "TcpClient.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QDateTime>
#include <QtGlobal>

TcpClient::TcpClient(QObject* parent)
    : QObject(parent)
{
    socket_ = new QSslSocket(this);
    // 强制 TLS 1.2+（优先 1.3），ECDSA 证书；禁用压缩
    socket_->setProtocol(QSsl::TlsV1_2OrLater);
    socket_->setPeerVerifyMode(QSslSocket::VerifyPeer);

    certPinner_ = new CertificatePinner(this);
    certPinner_->loadPins();   // 从 qrc :/server_cert.fingerprint 加载，可能为空

    codec_ = new ProtocolCodec(this);

    connect(socket_, &QSslSocket::readyRead,            this, &TcpClient::onReadyRead);
    connect(socket_, &QSslSocket::connected,            this, &TcpClient::onConnected);
    connect(socket_, &QSslSocket::disconnected,         this, &TcpClient::onDisconnected);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(socket_, &QSslSocket::errorOccurred,        this, &TcpClient::onSocketError);
#else
    connect(socket_,
            QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
            this, &TcpClient::onSocketError);
#endif
    connect(socket_,
            QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
            this, &TcpClient::onSslErrors);
    connect(socket_, &QSslSocket::peerVerifyError,      this, &TcpClient::onPeerVerifyError);

    heartbeatTimer_.setTimerType(Qt::VeryCoarseTimer);
    connect(&heartbeatTimer_, &QTimer::timeout, this, &TcpClient::onHeartbeatTimer);
    connect(&reconnectTimer_, &QTimer::timeout, this, &TcpClient::onReconnectTimer);
    reconnectDelayTimer_.setSingleShot(true);
    connect(&reconnectDelayTimer_, &QTimer::timeout, this, [this]() {
        if (!manualDisconnect_ && !serverHost_.isEmpty())
            connectToServer(serverHost_, serverPort_);
    });
}

TcpClient::~TcpClient()
{
    stopHeartbeat();
    if (socket_) {
        socket_->disconnect();
        socket_->abort();
    }
}

bool TcpClient::isConnected() const
{
    return socket_ && socket_->state() == QAbstractSocket::ConnectedState;
}

void TcpClient::setHeartbeatIntervalMs(int ms)
{
    heartbeatIntervalMs_ = ms > 0 ? ms : 30 * 1000;
    if (heartbeatTimer_.isActive())
        heartbeatTimer_.start(heartbeatIntervalMs_);
}

void TcpClient::setReconnectTimeoutMs(int ms)
{
    reconnectTimeoutMs_ = ms > 0 ? ms : 90 * 1000;
}

// ---------------- 连接 / 断开 ----------------
void TcpClient::connectToServer(const QString& host, quint16 port)
{
    serverHost_ = host;
    serverPort_ = port;
    manualDisconnect_ = false;
    buffer_.clear();
    lastError_.clear();
    if (!socket_) return;
    if (socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->abort();
    }
    socket_->connectToHostEncrypted(host, port);
}

void TcpClient::disconnectFromServer()
{
    manualDisconnect_ = true;
    reconnectDelayTimer_.stop();
    stopHeartbeat();
    if (socket_) socket_->disconnectFromHost();
}

// ---------------- 发送 ----------------
void TcpClient::sendMessage(const AIWZQ::Message& msg)
{
    if (!isConnected()) {
        lastError_ = QStringLiteral("socket not connected");
        return;
    }
    const QByteArray frame = ProtocolCodec::encodeFrame(msg);
    if (frame.isEmpty()) return;
    socket_->write(frame);
    socket_->flush();
}

void TcpClient::sendMessage(const QJsonObject& msg)
{
    if (!isConnected()) {
        lastError_ = QStringLiteral("socket not connected");
        return;
    }
    const QByteArray frame = ProtocolCodec::encodeFrame(msg);
    if (frame.isEmpty()) return;
    socket_->write(frame);
    socket_->flush();
}

// ---------------- 接收 / 拆包 ----------------
void TcpClient::onReadyRead()
{
    if (!socket_) return;
    buffer_.append(socket_->readAll());
    pumpFrames();
}

void TcpClient::pumpFrames()
{
    while (buffer_.size() >= 4) {
        AIWZQ::Message msg;
        size_t used = 0;
        bool ok = false;
        try {
            ok = ProtocolCodec::decodeFrame(buffer_.constData(),
                                            static_cast<size_t>(buffer_.size()), msg, used);
        } catch (const std::exception& e) {
            lastError_ = QString::fromUtf8(e.what());
            qWarning() << "[TcpClient] decode error:" << lastError_;
            buffer_.clear();
            emit error(QAbstractSocket::NetworkError);
            emit errorOccurred(QAbstractSocket::NetworkError);
            return;
        }
        if (!ok) break;   // 半包，继续接收
        buffer_.remove(0, static_cast<int>(used));
        noteActivity();
        const QJsonObject obj = ProtocolCodec::toQJson(msg);

        // 心跳响应：只刷新活跃状态，不上报业务层
        if (msg.type == AIWZQ::MsgType::HEARTBEAT_RESP) {
            heartbeatOutstanding_ = false;
            reconnectTimer_.stop();
        }
        emit messageReceived(obj);
    }
}

// ---------------- 连接回调 ----------------
void TcpClient::onConnected()
{
    lastError_.clear();
    buffer_.clear();
    noteActivity();
    startHeartbeat();
    emit connected();
}

void TcpClient::onDisconnected()
{
    stopHeartbeat();
    emit disconnected();
    if (!manualDisconnect_) scheduleReconnect();
}

void TcpClient::onSocketError(QAbstractSocket::SocketError err)
{
    lastError_ = socket_ ? socket_->errorString() : QStringLiteral("socket error");
    qWarning() << "[TcpClient] socket error" << err << lastError_;
    emit error(err);
    emit errorOccurred(err);
}

void TcpClient::onSslErrors(const QList<QSslError>& errors)
{
    for (const QSslError& e : errors)
        qWarning() << "[TcpClient] ssl error:" << int(e.error()) << e.errorString();

    // 证书固定：用 sslErrors 自带的证书校验指纹（sslErrors 阶段 peerCertificate()
    // 在部分 Qt 版本可能为空，而 e.certificate() 始终可用）。
    if (certPinner_ && !certPinner_->pins().isEmpty()) {
        bool pinned = false;
        for (const QSslError& e : errors) {
            if (certPinner_->isPinned(e.certificate())) { pinned = true; break; }
        }
        if (!pinned) pinned = certPinner_->isPinned(socket_->peerCertificate());

        if (pinned) {
            // 指纹命中白名单：自签/不可信/主机名错误全部可忽略，握手继续
            qInfo() << "[TcpClient] certificate pin matched, ignoring SSL errors";
            socket_->ignoreSslErrors(errors);
            return;
        }
        // 指纹不匹配：立即中断，防止中间人
        lastError_ = QStringLiteral("certificate pin mismatch");
        emit sslPinFailed(CertificatePinner::fingerprintOf(socket_->peerCertificate()));
        socket_->abort();
        return;
    }

    // 未配置 pin：退回 shouldIgnoreSslErrors 的默认逻辑
    if (certPinner_ && certPinner_->shouldIgnoreSslErrors(errors, socket_)) {
        socket_->ignoreSslErrors(errors);
    }
}

void TcpClient::onPeerVerifyError(const QSslError& error)
{
    qWarning() << "[TcpClient] peer verify error:" << int(error.error()) << error.errorString();
}

// ---------------- 心跳 / 重连 ----------------
void TcpClient::startHeartbeat()
{
    heartbeatOutstanding_ = false;
    heartbeatTimer_.start(heartbeatIntervalMs_);
    reconnectTimer_.setSingleShot(true);
}

void TcpClient::stopHeartbeat()
{
    heartbeatTimer_.stop();
    reconnectTimer_.stop();
}

void TcpClient::noteActivity()
{
    lastActivityMs_ = QDateTime::currentMSecsSinceEpoch();
}

void TcpClient::onHeartbeatTimer()
{
    if (!isConnected()) return;
    // 若上次 heartbeat 尚未回应，检查是否超过 90s
    if (heartbeatOutstanding_) {
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - lastActivityMs_;
        if (elapsed >= reconnectTimeoutMs_) {
            qWarning() << "[TcpClient] heartbeat timeout, reconnecting";
            socket_->abort();
            if (!manualDisconnect_) scheduleReconnect();
            return;
        }
    }
    QJsonObject data;
    data.insert(QStringLiteral("timestamp"), QDateTime::currentSecsSinceEpoch());
    const AIWZQ::Message m = codec_->build(AIWZQ::MsgType::HEARTBEAT_REQ, data);
    sendMessage(m);
    heartbeatOutstanding_ = true;
    reconnectTimer_.start(reconnectTimeoutMs_);
}

void TcpClient::onReconnectTimer()
{
    if (!isConnected()) return;
    qWarning() << "[TcpClient] no heartbeat response for" << reconnectTimeoutMs_ << "ms, reconnecting";
    heartbeatOutstanding_ = false;
    socket_->abort();
    if (!manualDisconnect_) scheduleReconnect();
}

void TcpClient::scheduleReconnect()
{
    if (manualDisconnect_ || serverHost_.isEmpty()) return;
    if (reconnectDelayTimer_.isActive()) return;
    reconnectDelayTimer_.start(1000);   // 1s 后重连
}

void TcpClient::abortTimers()
{
    stopHeartbeat();
    reconnectDelayTimer_.stop();
}
