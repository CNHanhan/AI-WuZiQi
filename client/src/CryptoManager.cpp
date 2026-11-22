#include "CryptoManager.h"

#include <QByteArray>
#include <QString>

#include <vector>
#include <cstring>

namespace {
static std::vector<uint8_t> toVec(const QByteArray& b)
{
    return std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(b.constData()),
                                reinterpret_cast<const uint8_t*>(b.constData()) + b.size());
}
static QByteArray fromVec(const std::vector<uint8_t>& v)
{
    if (v.empty()) return QByteArray();
    return QByteArray(reinterpret_cast<const char*>(v.data()), static_cast<int>(v.size()));
}
static std::string toStd(const QByteArray& b) { return b.toStdString(); }
} // namespace

CryptoManager::CryptoManager(QObject* parent) : QObject(parent) {}
CryptoManager::~CryptoManager() = default;

// ---------------- 随机数 ----------------
QByteArray CryptoManager::randomBytes(int n)
{
    if (n <= 0) return QByteArray();
    const std::string s = AIWZQ::Crypto::randomBytes(static_cast<size_t>(n));
    return QByteArray(s.data(), static_cast<int>(s.size()));
}

QString CryptoManager::randomHex(int n)
{
    return QString::fromStdString(AIWZQ::Crypto::randomHex(static_cast<size_t>(n)));
}

QByteArray CryptoManager::randomIv12()
{
    return randomBytes(12);
}

// ---------------- AES-256-GCM + Base64 ----------------
bool CryptoManager::aesGcmEncrypt(const QByteArray& plain, const QByteArray& key32,
                                  QByteArray& iv, QByteArray& cipher, QByteArray& tag)
{
    if (key32.size() != 32) return false;
    iv = randomIv12();
    std::vector<uint8_t> c, t;
    const bool ok = AIWZQ::Crypto::aesGcmEncrypt(toVec(plain), toVec(key32), toVec(iv), c, t);
    if (!ok) return false;
    cipher = fromVec(c);
    tag = fromVec(t);
    return true;
}

bool CryptoManager::aesGcmDecrypt(const QByteArray& cipher, const QByteArray& key32,
                                  const QByteArray& iv, const QByteArray& tag,
                                  QByteArray& plain)
{
    if (key32.size() != 32 || tag.size() != 16) return false;
    std::vector<uint8_t> p;
    const bool ok = AIWZQ::Crypto::aesGcmDecrypt(toVec(cipher), toVec(key32), toVec(iv), toVec(tag), p);
    if (!ok) return false;
    plain = fromVec(p);
    return true;
}

QByteArray CryptoManager::encryptThenBase64(const QByteArray& plain, const QByteArray& key32)
{
    QByteArray iv, cipher, tag;
    if (!aesGcmEncrypt(plain, key32, iv, cipher, tag)) return QByteArray();
    // 拼接：[12B IV][16B Tag][cipher]
    QByteArray packed;
    packed.reserve(iv.size() + tag.size() + cipher.size());
    packed.append(iv);
    packed.append(tag);
    packed.append(cipher);
    return base64Encode(packed);
}

QByteArray CryptoManager::base64ThenDecrypt(const QByteArray& encoded, const QByteArray& key32)
{
    const QByteArray packed = base64Decode(encoded);
    if (packed.size() < 12 + 16) return QByteArray();
    const QByteArray iv    = packed.left(12);
    const QByteArray tag   = packed.mid(12, 16);
    const QByteArray cipher= packed.mid(28);
    QByteArray plain;
    return aesGcmDecrypt(cipher, key32, iv, tag, plain) ? plain : QByteArray();
}

// ---------------- HMAC-SHA256 ----------------
QByteArray CryptoManager::hmacSha256(const QByteArray& key, const QByteArray& data)
{
    return fromVec(AIWZQ::Crypto::hmacSha256(toVec(key), toVec(data)));
}

QString CryptoManager::signMessage(const QByteArray& key, const QByteArray& data)
{
    return QString::fromLatin1(hmacSha256(key, data).toHex());
}

bool CryptoManager::verifyMessage(const QByteArray& key, const QByteArray& data,
                                  const QString& hexSignature)
{
    const QByteArray expected = hmacSha256(key, data);
    const QByteArray got = QByteArray::fromHex(hexSignature.toLatin1());
    return constantTimeEquals(expected, got);
}

bool CryptoManager::constantTimeEquals(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (int i = 0; i < a.size(); ++i)
        diff |= static_cast<unsigned char>(a.at(i)) ^ static_cast<unsigned char>(b.at(i));
    return diff == 0;
}

// ---------------- ECDSA ----------------
QByteArray CryptoManager::ecdsaSign(const QByteArray& data, const QByteArray& privPem)
{
    return fromVec(AIWZQ::Crypto::ecdsaSign(toVec(data), toStd(privPem)));
}

bool CryptoManager::ecdsaVerify(const QByteArray& data, const QByteArray& sig, const QByteArray& pubPem)
{
    return AIWZQ::Crypto::ecdsaVerify(toVec(data), toVec(sig), toStd(pubPem));
}

// ---------------- ECDH ----------------
CryptoManager::KeyPair CryptoManager::generateEcKeyPair()
{
    const AIWZQ::Crypto::KeyPair kp = AIWZQ::Crypto::generateEcKeyPair();
    KeyPair out;
    out.privPem = QByteArray::fromStdString(kp.privPem);
    out.pubPem  = QByteArray::fromStdString(kp.pubPem);
    return out;
}

QByteArray CryptoManager::deriveSessionKey(const QByteArray& myPrivPem, const QByteArray& peerPubPem)
{
    const std::vector<uint8_t> secret = AIWZQ::Crypto::ecdhSharedSecret(toStd(myPrivPem), toStd(peerPubPem));
    if (secret.empty()) return QByteArray();
    // 共享密钥经 SHA-256 得到 32 字节 AES key（等同 HKDF 的简化提取）
    return fromVec(AIWZQ::Crypto::sha256(secret));
}

// ---------------- Argon2id ----------------
QString CryptoManager::hashPassword(const QString& password)
{
    return QString::fromStdString(AIWZQ::Crypto::hashPassword(password.toStdString()));
}

bool CryptoManager::verifyPassword(const QString& password, const QString& encodedHash)
{
    return AIWZQ::Crypto::verifyPassword(password.toStdString(), encodedHash.toStdString());
}

// ---------------- 摘要 / 编码 ----------------
QByteArray CryptoManager::sha256(const QByteArray& data)
{
    return fromVec(AIWZQ::Crypto::sha256(toVec(data)));
}

QByteArray CryptoManager::base64Encode(const QByteArray& in)
{
    const std::string s = AIWZQ::Crypto::base64Encode(toVec(in));
    return QByteArray(s.data(), static_cast<int>(s.size()));
}

QByteArray CryptoManager::base64Decode(const QByteArray& in)
{
    const std::vector<uint8_t> v = AIWZQ::Crypto::base64Decode(in.toStdString());
    return fromVec(v);
}

// ---------------- 转换辅助 ----------------
QByteArray CryptoManager::toBytes(const QString& s) { return s.toUtf8(); }
QString CryptoManager::fromBytes(const QByteArray& b) { return QString::fromUtf8(b); }
