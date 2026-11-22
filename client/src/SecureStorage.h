#pragma once

// 安全存储：Windows 用 Credential Manager（CredWrite/CredRead/CredDelete），
// 非 Windows 用 QSettings 兜底。存储登录 token 与用户名/token 组合。

#include <QObject>
#include <QString>
#include <QPair>

class SecureStorage : public QObject
{
    Q_OBJECT
public:
    explicit SecureStorage(QObject* parent = nullptr);
    ~SecureStorage() override;

    // ---- Token ----
    static bool saveToken(const QString& token);
    static QString loadToken();
    static void deleteToken();

    // ---- 凭据对（username + token）----
    static bool saveCredentials(const QString& username, const QString& token);
    static QPair<QString, QString> loadCredentials();
    static void deleteCredentials();

private:
    // Credential Manager 使用的目标名（TargetName）
    static QString tokenTarget();
    static QString credentialsTarget();
};
