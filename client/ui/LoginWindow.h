#pragma once

// LoginWindow — 登录/注册窗口（含找回密码对话框）
// 只采集用户输入并通过 signals 抛出意图，绝不 include 网络层。

#include <QMainWindow>
#include <QDialog>
#include <QString>

class QLineEdit;
class QPushButton;
class QLabel;
class FindPasswordDialog;

class LoginWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit LoginWindow(QWidget *parent = nullptr);

    // ---------- 供 controller 回写结果 ----------
    void showMessage(const QString &msg);       // 状态提示（成功/失败均可）
    void setBusy(bool busy);                     // 登录/注册进行中，禁用按钮

signals:
    void loginRequested(const QString &username, const QString &password);
    void registerRequested(const QString &username,
                           const QString &password,
                           const QString &email);
    void findPwdRequested(const QString &email);
    void verifyCodeRequested(const QString &email);
    void resetPwdRequested(const QString &email,
                           const QString &code,
                           const QString &newPassword);
    void offlineGameRequested();

private slots:
    void onLoginClicked();
    void onRegisterClicked();
    void onOpenFindPwd();
    void onOfflineClicked();
    void onVerifyCodeClicked();
    void onSubmitResetClicked();

private:
    bool validateInput();
    void setupUi();

    QLineEdit *usernameEdit_;
    QLineEdit *passwordEdit_;
    QLineEdit *emailEdit_;               // 注册邮箱
    QPushButton *loginBtn_;
    QPushButton *registerBtn_;
    QPushButton *findPwdBtn_;
    QPushButton *offlineBtn_;
    QLabel     *statusLabel_;

    FindPasswordDialog *findPwdDialog_;
};

// ---------------------------------------------------------------------------
// 找回密码对话框：邮箱 -> 验证码 -> 新密码
// ---------------------------------------------------------------------------
class FindPasswordDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FindPasswordDialog(QWidget *parent = nullptr);

    void showMessage(const QString &msg);
    void setBusy(bool busy);

signals:
    void verifyCodeRequested(const QString &email);
    void resetPwdRequested(const QString &email,
                           const QString &code,
                           const QString &newPassword);

private slots:
    void onSendCodeClicked();
    void onSubmitClicked();

private:
    void setupUi();

    QLineEdit *emailEdit_;
    QLineEdit *codeEdit_;
    QLineEdit *newPwdEdit_;
    QLineEdit *confirmPwdEdit_;
    QPushButton *sendCodeBtn_;
    QPushButton *submitBtn_;
    QLabel      *statusLabel_;
};
