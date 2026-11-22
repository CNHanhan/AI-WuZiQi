#pragma once

// CryptoManager：调用冻结头 common/include/Crypto.h（AIWZQ::Crypto 命名空间）。
// 提供：AES-256-GCM 加密 + Base64、HMAC-SHA256 签名、ECDSA 验签、
//       Argon2id 密码哈希、ECDH 会话密钥派生、随机数/证书指纹辅助。

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QPair>

#include "Crypto.h"   // AIWZQ::Crypto

class CryptoManager : public QObject
{
    Q_OBJECT
public:
    explicit CryptoManager(QObject* parent = nullptr);
    ~CryptoManager() override;

    // ---- 随机数 ----
    static QByteArray randomBytes(int n);                 // 原始随机字节
    static QString   randomHex(int n);                    // 2n 长度 hex（nonce）
    static QByteArray randomIv12();                       // 12 字节 GCM IV（推荐值）

    // ---- AES-256-GCM + Base64 ----
    // 输出格式（Base64 之前的原始字节）：[12B IV][16B Tag][CipherText...]
    // key 必须 32 字节；失败返回空 QByteArray
    static QByteArray encryptThenBase64(const QByteArray& plain, const QByteArray& key32);
    static QByteArray base64ThenDecrypt(const QByteArray& encoded, const QByteArray& key32);
    // 拆开的底层接口
    static bool aesGcmEncrypt(const QByteArray& plain, const QByteArray& key32,
                              QByteArray& iv, QByteArray& cipher, QByteArray& tag);
    static bool aesGcmDecrypt(const QByteArray& cipher, const QByteArray& key32,
                              const QByteArray& iv, const QByteArray& tag,
                              QByteArray& plain);

    // ---- HMAC-SHA256 消息签名 ----
    static QByteArray hmacSha256(const QByteArray& key, const QByteArray& data); // 原始 32 字节
    static QString   signMessage(const QByteArray& key, const QByteArray& data); // hex 字符串
    static bool      verifyMessage(const QByteArray& key, const QByteArray& data,
                                   const QString& hexSignature);
    // 常量时间比较
    static bool constantTimeEquals(const QByteArray& a, const QByteArray& b);

    // ---- ECDSA P-256 ----
    static QByteArray ecdsaSign(const QByteArray& data, const QByteArray& privPem);
    static bool ecdsaVerify(const QByteArray& data, const QByteArray& sig, const QByteArray& pubPem);

    // ---- ECDH 会话密钥派生（共享密钥 -> SHA256 得到 32B AES key）----
    struct KeyPair { QByteArray privPem; QByteArray pubPem; };
    static KeyPair generateEcKeyPair();
    static QByteArray deriveSessionKey(const QByteArray& myPrivPem, const QByteArray& peerPubPem);

    // ---- Argon2id 密码哈希（注册/登录前本地哈希）----
    static QString hashPassword(const QString& password);                 // 返回 PHC 编码串
    static bool verifyPassword(const QString& password, const QString& encodedHash);

    // ---- 摘要 / 编码 ----
    static QByteArray sha256(const QByteArray& data);
    static QByteArray base64Encode(const QByteArray& in);
    static QByteArray base64Decode(const QByteArray& in);

    // ---- 转换辅助 ----
    static QByteArray toBytes(const QString& s);                          // UTF-8
    static QString   fromBytes(const QByteArray& b);                      // UTF-8
};
