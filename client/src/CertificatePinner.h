#pragma once

// 证书固定：把服务端证书的 DER SHA-256 指纹写入白名单，握手期校验。
// 指纹来源：resources/server_cert.fingerprint（每行一个 64 hex，忽略 # 注释与空行）。

#include <QObject>
#include <QString>
#include <QStringList>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslError>

class CertificatePinner : public QObject
{
    Q_OBJECT
public:
    explicit CertificatePinner(QObject* parent = nullptr);

    // 从 qrc 资源 / 文件路径加载白名单指纹；返回成功加载的个数
    int loadPins(const QString& filePath = QStringLiteral(":/server_cert.fingerprint"));
    // 直接在内存设置一个允许的指纹（方便测试/热更新）
    void addPin(const QString& sha256Hex);
    void clearPins();

    // 计算单张证书 DER 的 SHA-256 指纹（64 位小写 hex）
    static QString fingerprintOf(const QSslCertificate& cert);
    // 判断证书是否命中白名单
    bool isPinned(const QSslCertificate& cert) const;
    bool isPinnedFingerprint(const QString& sha256Hex) const;

    // 对已建立/建立中的 QSslSocket 校验对等证书，命中白名单返回 true
    bool verifyCertificate(QSslSocket* socket) const;
    // 供 onSslErrors 使用：若错误仅因自签/主机名且证书已 pin，则可忽略该类错误
    // 返回 true 表示可以安全地忽略本次错误集合
    bool shouldIgnoreSslErrors(const QList<QSslError>& errors, QSslSocket* socket) const;

    QStringList pins() const { return pins_; }

private:
    QStringList pins_;   // 规整后的小写 hex（64 字符）
    static QString normalizeHex(QString s);

signals:
    void pinMismatch(const QString& observed, const QString& expectedList);
};
