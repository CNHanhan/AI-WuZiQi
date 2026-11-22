#include "ProtocolCodec.h"

#include "Crypto.h"   // AIWZQ::Crypto::randomHex

#include <QJsonDocument>
#include <QJsonArray>
#include <QMutexLocker>
#include <QDateTime>
#include <QtGlobal>

namespace {
// 递归转换：nlohmann json 的全部标量类型 -> QJsonValue
static QJsonValue nlohmannToQJsonValue(const AIWZQ::json& j)
{
    using AIWZQ::json;
    switch (j.type()) {
    case json::value_t::null:
        return QJsonValue(QJsonValue::Null);
    case json::value_t::boolean:
        return QJsonValue(j.get<bool>());
    case json::value_t::number_integer:
        return QJsonValue(static_cast<qint64>(j.get<int64_t>()));
    case json::value_t::number_unsigned:
        return QJsonValue(static_cast<qint64>(j.get<uint64_t>()));
    case json::value_t::number_float:
        return QJsonValue(j.get<double>());
    case json::value_t::string:
        return QJsonValue(QString::fromStdString(j.get<std::string>()));
    case json::value_t::array: {
        QJsonArray arr;
        for (const auto& item : j) arr.append(nlohmannToQJsonValue(item));
        return QJsonValue(arr);
    }
    case json::value_t::object: {
        QJsonObject obj;
        for (auto it = j.begin(); it != j.end(); ++it)
            obj.insert(QString::fromStdString(it.key()), nlohmannToQJsonValue(it.value()));
        return QJsonValue(obj);
    }
    case json::value_t::discarded:
    default:
        return QJsonValue(QJsonValue::Undefined);
    }
}

static AIWZQ::json qjsonValueToNlohmann(const QJsonValue& v)
{
    using AIWZQ::json;
    switch (v.type()) {
    case QJsonValue::Null:
        return json(nullptr);
    case QJsonValue::Bool:
        return json(v.toBool());
    case QJsonValue::Double:
        return json(v.toDouble());
    case QJsonValue::String:
        return json(v.toString().toStdString());
    case QJsonValue::Array: {
        json j = json::array();
        const QJsonArray arr = v.toArray();
        for (const QJsonValue& item : arr)
            j.push_back(qjsonValueToNlohmann(item));
        return j;
    }
    case QJsonValue::Object: {
        json j = json::object();
        const QJsonObject obj = v.toObject();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
            j[it.key().toStdString()] = qjsonValueToNlohmann(it.value());
        return j;
    }
    case QJsonValue::Undefined:
    default:
        return json(nullptr);
    }
}
} // namespace

ProtocolCodec::ProtocolCodec(QObject* parent)
    : QObject(parent), seq_(0)
{
}

ProtocolCodec::~ProtocolCodec() = default;

qint64 ProtocolCodec::nextSeq()
{
    QMutexLocker locker(&seqMutex_);
    ++seq_;
    if (seq_ <= 0) seq_ = 1;   // 回绕保护
    return seq_;
}

AIWZQ::Message ProtocolCodec::build(const QString& type, const QJsonObject& data)
{
    return build(type.toStdString(), data);
}

AIWZQ::Message ProtocolCodec::build(const char* type, const QJsonObject& data)
{
    return build(std::string(type ? type : ""), data);
}

AIWZQ::Message ProtocolCodec::build(const std::string& type, const QJsonObject& data)
{
    AIWZQ::Message m;
    m.type      = type;
    m.seq       = nextSeq();
    m.nonce     = AIWZQ::Crypto::randomHex(16);           // 32 hex chars
    m.timestamp = static_cast<int64_t>(QDateTime::currentSecsSinceEpoch());
    m.data      = fromQJson(data);
    return m;
}

QJsonObject ProtocolCodec::parse(const AIWZQ::Message& msg)
{
    return toQJson(msg);
}

QJsonObject ProtocolCodec::toQJson(const AIWZQ::Message& msg)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"),      QString::fromStdString(msg.type));
    o.insert(QStringLiteral("seq"),        QJsonValue(static_cast<qint64>(msg.seq)));
    o.insert(QStringLiteral("nonce"),      QString::fromStdString(msg.nonce));
    o.insert(QStringLiteral("timestamp"),  QJsonValue(static_cast<qint64>(msg.timestamp)));
    o.insert(QStringLiteral("data"),       toQJsonValue(msg.data));
    return o;
}

QJsonObject ProtocolCodec::dataOf(const AIWZQ::Message& msg)
{
    return fromNlohmann(msg.data);
}

QJsonObject ProtocolCodec::fromNlohmann(const AIWZQ::json& j)
{
    if (j.is_object()) return nlohmannToQJsonValue(j).toObject();
    return QJsonObject();
}

AIWZQ::json ProtocolCodec::fromQJson(const QJsonObject& obj)
{
    AIWZQ::json j = AIWZQ::json::object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        j[it.key().toStdString()] = qjsonValueToNlohmann(it.value());
    return j;
}

QJsonValue ProtocolCodec::toQJsonValue(const AIWZQ::json& j)
{
    return nlohmannToQJsonValue(j);
}

AIWZQ::json ProtocolCodec::fromQJsonValue(const QJsonValue& v)
{
    return qjsonValueToNlohmann(v);
}

QByteArray ProtocolCodec::encodeFrame(const AIWZQ::Message& msg)
{
    const std::string frame = AIWZQ::encodeFrame(msg);
    return QByteArray(frame.data(), static_cast<int>(frame.size()));
}

QByteArray ProtocolCodec::encodeFrame(const QJsonObject& obj)
{
    // 与冻结头同格式：[4B 大端长度][JSON UTF-8]
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    const quint32 len = static_cast<quint32>(payload.size());
    QByteArray out;
    out.reserve(4 + payload.size());
    out.append(static_cast<char>((len >> 24) & 0xFF));
    out.append(static_cast<char>((len >> 16) & 0xFF));
    out.append(static_cast<char>((len >> 8)  & 0xFF));
    out.append(static_cast<char>( len        & 0xFF));
    out.append(payload);
    return out;
}

bool ProtocolCodec::decodeFrame(const char* buf, size_t len, AIWZQ::Message& msg, size_t& used)
{
    return AIWZQ::decodeOneFrame(buf, len, msg, used);
}
