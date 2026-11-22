#pragma once

// TcpClient：QSslSocket 封装（TLS 8889）。
//  - 帧格式 [4B 大端长度][JSON]，QByteArray 缓冲 + AIWZQ::decodeOneFrame 逻辑
//  - 心跳 30s 一次 heartbeat_req，90s 无响应重连
//  - onSslErrors 证书固定（CertificatePinner）
//  - 收到业务消息后经信号 messageReceived(QJsonObject) 上报

#include <QObject>
#include <QSslSocket>
#include <QByteArray>
#include <QString>
#include <QTimer>
#include <QJsonObject>
#include <QList>
#include <QSslError>

#include "ProtocolCodec.h"
#include "CertificatePinner.h"

class TcpClient : public QObject
{
    Q_OBJECT
public:
    explicit TcpClient(QObject* parent = nullptr);
    ~TcpClient() override;

    // 证书固定器（外部可注入指纹；默认从 :/server_cert.fingerprint 加载）
    CertificatePinner* pinner() const { return certPinner_; }

    bool isConnected() const;
    QString lastErrorString() const { return lastError_; }

    // 配置心跳/超时（默认 30s / 90s，便于测试调整）
    void setHeartbeatIntervalMs(int ms);
    void setReconnectTimeoutMs(int ms);

public slots:
    void connectToServer(const QString& host, quint16 port);
    void disconnectFromServer();
    // 发送一个完整 QJsonObject 消息（自动按帧编码写出）
    void sendMessage(const QJsonObject& msg);
    // 便捷：直接用协议生成器构造并发送
    void sendMessage(const AIWZQ::Message& msg);

signals:
    void messageReceived(const QJsonObject& msg);
    void connected();
    void disconnected();
    void error(QAbstractSocket::SocketError error);
    // Qt 风格别名，给偏好 errorOccurred 的调用方
    void errorOccurred(QAbstractSocket::SocketError error);
    void sslPinFailed(const QString& observedFingerprint);

private slots:
    void onReadyRead();
    void onConnected();
    void onDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onSslErrors(const QList<QSslError>& errors);
    void onPeerVerifyError(const QSslError& error);
    void onHeartbeatTimer();
    void onReconnectTimer();

private:
    void pumpFrames();
    void startHeartbeat();
    void stopHeartbeat();
    void noteActivity();          // 收到任何合法帧都刷新"最后活跃"时间
    void scheduleReconnect();
    void abortTimers();

private:
    QSslSocket* socket_ = nullptr;
    QByteArray  buffer_;
    QString     serverHost_;
    quint16     serverPort_ = 8889;
    QString     lastError_;

    CertificatePinner* certPinner_ = nullptr;
    ProtocolCodec*     codec_ = nullptr;

    QTimer heartbeatTimer_;
    QTimer reconnectTimer_;      // 90s 无响应触发
    QTimer reconnectDelayTimer_; // 断线后的延迟重连
    bool   manualDisconnect_ = false;

    int heartbeatIntervalMs_ = 30 * 1000;
    int reconnectTimeoutMs_  = 90 * 1000;
    qint64 lastActivityMs_ = 0;
    bool   heartbeatOutstanding_ = false;   // 已发 heartbeat_req 但未收到 resp
};
