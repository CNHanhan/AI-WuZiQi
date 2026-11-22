#pragma once

// 协议编解码：QJsonObject <-> AIWZQ::Message，并负责通用字段填充。
// 依赖冻结头 common/include/Protocol.h、Types.h、Crypto.h（不改冻结头）。

#include <QObject>
#include <QJsonObject>
#include <QJsonValue>
#include <QByteArray>
#include <QString>
#include <QMutex>

#include "Protocol.h"   // AIWZQ::Message / AIWZQ::json(nlohmann) / decodeOneFrame

class ProtocolCodec : public QObject
{
    Q_OBJECT
public:
    explicit ProtocolCodec(QObject* parent = nullptr);
    ~ProtocolCodec() override;

    // ---- 构造消息：自动填 seq / nonce / timestamp ----
    // nonce   = AIWZQ::Crypto::randomHex(16) -> 32 个 hex 字符
    // timestamp = 当前 Unix 秒
    // seq     = 进程内自增序列号（从 1 开始，线程安全）
    AIWZQ::Message build(const QString& type, const QJsonObject& data = QJsonObject());
    AIWZQ::Message build(const char*     type, const QJsonObject& data = QJsonObject());
    AIWZQ::Message build(const std::string& type, const QJsonObject& data = QJsonObject());

    // ---- 解析 ----
    // 返回 data 业务对象（最常用，供 GameController 分派）
    static QJsonObject parse(const AIWZQ::Message& msg);
    // 返回完整 JSON 对象（含 type/seq/nonce/timestamp/data）
    static QJsonObject toQJson(const AIWZQ::Message& msg);
    // 只取 Message.data 转 QJsonObject
    static QJsonObject dataOf(const AIWZQ::Message& msg);

    // ---- nlohmann::json <-> Qt JSON ----
    static QJsonObject    fromNlohmann(const AIWZQ::json& j);
    static AIWZQ::json    fromQJson(const QJsonObject& obj);
    static QJsonValue     toQJsonValue(const AIWZQ::json& j);
    static AIWZQ::json    fromQJsonValue(const QJsonValue& v);

    // ---- 帧编码/解码：[4B 大端长度][JSON UTF-8] ----
    // 直接复用冻结头 AIWZQ::encodeFrame 的格式
    static QByteArray encodeFrame(const AIWZQ::Message& msg);
    static QByteArray encodeFrame(const QJsonObject& obj);
    // 从字节流起始处尝试解一帧；true=成功并填充 msg 与消耗长度；false=半包；抛 DecodeError=非法
    static bool decodeFrame(const char* buf, size_t len, AIWZQ::Message& msg, size_t& used);

    // ---- 序列号 ----
    qint64 nextSeq();

private:
    QMutex seqMutex_;
    qint64 seq_;
};
