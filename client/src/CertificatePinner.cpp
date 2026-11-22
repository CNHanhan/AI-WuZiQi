#include "CertificatePinner.h"

#include <QCryptographicHash>
#include <QFile>
#include <QDebug>

CertificatePinner::CertificatePinner(QObject* parent)
    : QObject(parent)
{
}

QString CertificatePinner::normalizeHex(QString s)
{
    s.remove(QLatin1Char(':'));
    s.remove(QLatin1Char(' '));
    s.remove(QLatin1Char('\t'));
    return s.trimmed().toLower();
}

void CertificatePinner::addPin(const QString& sha256Hex)
{
    const QString h = normalizeHex(sha256Hex);
    if (h.size() == 64 && !pins_.contains(h, Qt::CaseInsensitive))
        pins_.append(h);
}

void CertificatePinner::clearPins()
{
    pins_.clear();
}

int CertificatePinner::loadPins(const QString& filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[CertificatePinner] cannot open pins file:" << filePath;
        return 0;
    }
    const int before = pins_.size();
    const QByteArray raw = f.readAll();
    const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith(QLatin1Char('#'))) continue;
        addPin(t);
    }
    return pins_.size() - before;
}

QString CertificatePinner::fingerprintOf(const QSslCertificate& cert)
{
    if (cert.isNull()) return QString();
    const QByteArray der = cert.toDer();
    if (der.isEmpty()) return QString();
    return QString::fromLatin1(QCryptographicHash::hash(der, QCryptographicHash::Sha256).toHex()).toLower();
}

bool CertificatePinner::isPinnedFingerprint(const QString& sha256Hex) const
{
    if (pins_.isEmpty()) return true;   // 未配置 pin 时不阻断（由调用方决定是否要求强制）
    const QString h = normalizeHex(sha256Hex);
    return pins_.contains(h, Qt::CaseInsensitive);
}

bool CertificatePinner::isPinned(const QSslCertificate& cert) const
{
    const QString fp = fingerprintOf(cert);
    return !fp.isEmpty() && isPinnedFingerprint(fp);
}

bool CertificatePinner::verifyCertificate(QSslSocket* socket) const
{
    if (!socket) return false;
    const QSslCertificate cert = socket->peerCertificate();
    if (cert.isNull()) return false;
    if (pins_.isEmpty()) return true;   // 未配置 pin：退回系统信任链
    const QString fp = fingerprintOf(cert);
    if (!pins_.contains(fp, Qt::CaseInsensitive)) {
        emit const_cast<CertificatePinner*>(this)->pinMismatch(fp, pins_.join(QLatin1Char(',')));
        return false;
    }
    return true;
}

bool CertificatePinner::shouldIgnoreSslErrors(const QList<QSslError>& errors, QSslSocket* socket) const
{
    if (pins_.isEmpty()) return false;
    if (!socket) return false;
    if (!isPinned(socket->peerCertificate())) return false;

    // 只有以下错误类型可在证书已被 pin 的前提下忽略；
    // 其余（如证书已过期、主机名不匹配且未 pin 等）一律保留。
    for (const QSslError& e : errors) {
        switch (e.error()) {
        case QSslError::SelfSignedCertificate:
        case QSslError::SelfSignedCertificateInChain:
        case QSslError::HostNameMismatch:
        case QSslError::CertificateUntrusted:
        case QSslError::UnableToGetLocalIssuerCertificate:
        case QSslError::UnableToVerifyFirstCertificate:
            continue;
        default:
            return false;
        }
    }
    return true;
}
