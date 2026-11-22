#include "SecureStorage.h"

#include <QSettings>
#include <QDebug>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincred.h>
#endif

namespace {
const char* kOrg  = "AI-WuZiQi";
const char* kApp  = "Client";
}

SecureStorage::SecureStorage(QObject* parent) : QObject(parent) {}
SecureStorage::~SecureStorage() = default;

QString SecureStorage::tokenTarget()
{
    return QStringLiteral("AI-WuZiQi/token");
}

QString SecureStorage::credentialsTarget()
{
    return QStringLiteral("AI-WuZiQi/credentials");
}

// ---------------- Windows: Credential Manager ----------------
#ifdef Q_OS_WIN
namespace {
// 把 QString 转成 Credential blob（UTF-16LE，Credential Manager 保存为字节）
static QByteArray toBlob(const QString& s)
{
    return QByteArray(reinterpret_cast<const char*>(s.utf16()),
                      s.size() * static_cast<int>(sizeof(ushort)));
}
static QString fromBlob(const CREDENTIALA* cred)
{
    if (!cred || !cred->CredentialBlob || cred->CredentialBlobSize == 0) return QString();
    const int chars = static_cast<int>(cred->CredentialBlobSize / sizeof(ushort));
    return QString::fromUtf16(reinterpret_cast<const ushort*>(cred->CredentialBlob), chars);
}
static bool credWrite(const QString& target, const QString& user, const QString& secret)
{
    // 注意：QByteArray 必须是局部生存期变量，保证 CredWriteA 调用期间指针有效
    const QByteArray targetBytes = target.toUtf8();
    const QByteArray userBytes   = user.toUtf8();
    const QByteArray blob        = toBlob(secret);
    CREDENTIALA cred;
    ZeroMemory(&cred, sizeof(cred));
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<char*>(targetBytes.constData());
    cred.UserName = const_cast<char*>(userBytes.constData());
    cred.CredentialBlob = reinterpret_cast<BYTE*>(const_cast<char*>(blob.constData()));
    cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteA(&cred, 0) == TRUE;
}
static QString credRead(const QString& target)
{
    CREDENTIALA* pCred = nullptr;
    if (CredReadA(target.toUtf8().constData(), CRED_TYPE_GENERIC, 0, &pCred) != TRUE)
        return QString();
    const QString val = fromBlob(pCred);
    CredFree(pCred);
    return val;
}
static bool credDelete(const QString& target)
{
    return CredDeleteA(target.toUtf8().constData(), CRED_TYPE_GENERIC, 0) == TRUE;
}
} // namespace
#endif

// ---------------- Token ----------------
bool SecureStorage::saveToken(const QString& token)
{
#ifdef Q_OS_WIN
    if (credWrite(tokenTarget(), QStringLiteral("token"), token)) return true;
    qWarning() << "[SecureStorage] CredWrite token failed, fallback to QSettings";
#endif
    QSettings s(kOrg, kApp);
    s.setValue(QStringLiteral("secure/token"), token);
    s.sync();
    return s.status() == QSettings::NoError;
}

QString SecureStorage::loadToken()
{
#ifdef Q_OS_WIN
    const QString t = credRead(tokenTarget());
    if (!t.isEmpty()) return t;
#endif
    QSettings s(kOrg, kApp);
    return s.value(QStringLiteral("secure/token")).toString();
}

void SecureStorage::deleteToken()
{
#ifdef Q_OS_WIN
    credDelete(tokenTarget());
#endif
    QSettings s(kOrg, kApp);
    s.remove(QStringLiteral("secure/token"));
    s.sync();
}

// ---------------- Credentials (username + token) ----------------
bool SecureStorage::saveCredentials(const QString& username, const QString& token)
{
    // 用 "username\ntoken" 的 blob 结构保存，读取时按第一个换行拆分
    const QString combined = username + QLatin1Char('\n') + token;
#ifdef Q_OS_WIN
    if (credWrite(credentialsTarget(), username, combined)) return true;
    qWarning() << "[SecureStorage] CredWrite credentials failed, fallback to QSettings";
#endif
    QSettings s(kOrg, kApp);
    s.setValue(QStringLiteral("secure/credentials"), combined);
    s.sync();
    return s.status() == QSettings::NoError;
}

QPair<QString, QString> SecureStorage::loadCredentials()
{
    QString combined;
#ifdef Q_OS_WIN
    combined = credRead(credentialsTarget());
#endif
    if (combined.isEmpty()) {
        QSettings s(kOrg, kApp);
        combined = s.value(QStringLiteral("secure/credentials")).toString();
    }
    const int pos = combined.indexOf(QLatin1Char('\n'));
    if (pos < 0) return qMakePair(QString(), QString());
    return qMakePair(combined.left(pos), combined.mid(pos + 1));
}

void SecureStorage::deleteCredentials()
{
#ifdef Q_OS_WIN
    credDelete(credentialsTarget());
#endif
    QSettings s(kOrg, kApp);
    s.remove(QStringLiteral("secure/credentials"));
    s.sync();
}
