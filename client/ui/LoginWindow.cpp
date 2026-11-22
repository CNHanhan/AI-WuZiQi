#include "LoginWindow.h"

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QRegularExpression>
#include <QPalette>
#include <QFont>

// ---------------------------------------------------------------------------
// LoginWindow
// ---------------------------------------------------------------------------
LoginWindow::LoginWindow(QWidget *parent)
    : QMainWindow(parent)
    , findPwdDialog_(nullptr)
{
    setupUi();
}

void LoginWindow::setupUi()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    usernameEdit_ = new QLineEdit(central);
    usernameEdit_->setPlaceholderText(QStringLiteral("用户名"));
    passwordEdit_ = new QLineEdit(central);
    passwordEdit_->setPlaceholderText(QStringLiteral("密码"));
    passwordEdit_->setEchoMode(QLineEdit::Password);
    emailEdit_ = new QLineEdit(central);
    emailEdit_->setPlaceholderText(QStringLiteral("邮箱（注册用）"));

    loginBtn_     = new QPushButton(QStringLiteral("登录"), central);
    registerBtn_  = new QPushButton(QStringLiteral("注册"), central);
    findPwdBtn_   = new QPushButton(QStringLiteral("找回密码"), central);
    offlineBtn_   = new QPushButton(QStringLiteral("离线对战"), central);
    statusLabel_  = new QLabel(central);
    statusLabel_->setMinimumHeight(24);

    QLabel *logo = new QLabel(QStringLiteral("AI-WuZiQi 五子棋对战平台"), central);
    logo->setAlignment(Qt::AlignCenter);
    QFont f = logo->font();
    f.setPointSize(18);
    f.setBold(true);
    logo->setFont(f);

    QFormLayout *form = new QFormLayout;
    form->addRow(QStringLiteral("用户名"), usernameEdit_);
    form->addRow(QStringLiteral("密码"), passwordEdit_);
    form->addRow(QStringLiteral("邮箱"), emailEdit_);

    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->addWidget(loginBtn_);
    btnRow->addWidget(registerBtn_);

    QHBoxLayout *linkRow = new QHBoxLayout;
    linkRow->addWidget(findPwdBtn_);
    linkRow->addWidget(offlineBtn_);

    QVBoxLayout *main = new QVBoxLayout(central);
    main->addStretch(1);
    main->addWidget(logo);
    main->addSpacing(20);
    main->addLayout(form);
    main->addSpacing(8);
    main->addLayout(btnRow);
    main->addLayout(linkRow);
    main->addWidget(statusLabel_);
    main->addStretch(1);

    setWindowTitle(QStringLiteral("登录 - AI-WuZiQi"));
    resize(420, 380);

    connect(loginBtn_, &QPushButton::clicked, this, &LoginWindow::onLoginClicked);
    connect(registerBtn_, &QPushButton::clicked, this, &LoginWindow::onRegisterClicked);
    connect(findPwdBtn_, &QPushButton::clicked, this, &LoginWindow::onOpenFindPwd);
    connect(offlineBtn_, &QPushButton::clicked, this, &LoginWindow::onOfflineClicked);

    // 回车登录
    connect(passwordEdit_, &QLineEdit::returnPressed, this, &LoginWindow::onLoginClicked);
}

bool LoginWindow::validateInput()
{
    if (usernameEdit_->text().trimmed().isEmpty()) {
        showMessage(QStringLiteral("请输入用户名"));
        return false;
    }
    if (passwordEdit_->text().isEmpty()) {
        showMessage(QStringLiteral("请输入密码"));
        return false;
    }
    return true;
}

void LoginWindow::onLoginClicked()
{
    if (!validateInput())
        return;
    emit loginRequested(usernameEdit_->text().trimmed(), passwordEdit_->text());
}

void LoginWindow::onRegisterClicked()
{
    if (!validateInput())
        return;
    QString email = emailEdit_->text().trimmed();
    QRegularExpression rx(QStringLiteral("^[^@]+@[^@]+\\.[^@]+$"));
    if (!rx.match(email).hasMatch()) {
        showMessage(QStringLiteral("请输入有效邮箱"));
        return;
    }
    emit registerRequested(usernameEdit_->text().trimmed(), passwordEdit_->text(), email);
}

void LoginWindow::onOpenFindPwd()
{
    if (!findPwdDialog_) {
        findPwdDialog_ = new FindPasswordDialog(this);
        // 把对话框的两级信号转发到窗口的统一出口，方便 net agent 只连一处
        connect(findPwdDialog_, &FindPasswordDialog::verifyCodeRequested,
                this, &LoginWindow::verifyCodeRequested);
        connect(findPwdDialog_, &FindPasswordDialog::resetPwdRequested,
                this, &LoginWindow::resetPwdRequested);
    }
    findPwdDialog_->show();
}

void LoginWindow::onOfflineClicked()
{
    emit offlineGameRequested();
}

void LoginWindow::showMessage(const QString &msg)
{
    statusLabel_->setText(msg);
}

void LoginWindow::setBusy(bool busy)
{
    loginBtn_->setDisabled(busy);
    registerBtn_->setDisabled(busy);
}

// ---------------------------------------------------------------------------
// FindPasswordDialog
// ---------------------------------------------------------------------------
FindPasswordDialog::FindPasswordDialog(QWidget *parent)
    : QDialog(parent)
{
    setupUi();
}

void FindPasswordDialog::setupUi()
{
    emailEdit_ = new QLineEdit(this);
    emailEdit_->setPlaceholderText(QStringLiteral("注册邮箱"));
    codeEdit_ = new QLineEdit(this);
    codeEdit_->setPlaceholderText(QStringLiteral("验证码"));
    newPwdEdit_ = new QLineEdit(this);
    newPwdEdit_->setPlaceholderText(QStringLiteral("新密码"));
    newPwdEdit_->setEchoMode(QLineEdit::Password);
    confirmPwdEdit_ = new QLineEdit(this);
    confirmPwdEdit_->setPlaceholderText(QStringLiteral("确认新密码"));
    confirmPwdEdit_->setEchoMode(QLineEdit::Password);

    sendCodeBtn_ = new QPushButton(QStringLiteral("发送验证码"), this);
    submitBtn_   = new QPushButton(QStringLiteral("提交重置"), this);
    statusLabel_ = new QLabel(this);
    statusLabel_->setMinimumHeight(22);

    QHBoxLayout *codeRow = new QHBoxLayout;
    codeRow->addWidget(codeEdit_);
    codeRow->addWidget(sendCodeBtn_);

    QFormLayout *form = new QFormLayout;
    form->addRow(QStringLiteral("邮箱"), emailEdit_);
    form->addRow(QStringLiteral("验证码"), codeRow);
    form->addRow(QStringLiteral("新密码"), newPwdEdit_);
    form->addRow(QStringLiteral("确认密码"), confirmPwdEdit_);

    QVBoxLayout *main = new QVBoxLayout(this);
    main->addWidget(new QLabel(QStringLiteral("找回密码"), this));
    main->addLayout(form);
    main->addWidget(submitBtn_);
    main->addWidget(statusLabel_);

    setWindowTitle(QStringLiteral("找回密码"));
    resize(380, 260);

    connect(sendCodeBtn_, &QPushButton::clicked, this, &FindPasswordDialog::onSendCodeClicked);
    connect(submitBtn_, &QPushButton::clicked, this, &FindPasswordDialog::onSubmitClicked);
}

void FindPasswordDialog::onSendCodeClicked()
{
    QString email = emailEdit_->text().trimmed();
    QRegularExpression rx(QStringLiteral("^[^@]+@[^@]+\\.[^@]+$"));
    if (!rx.match(email).hasMatch()) {
        showMessage(QStringLiteral("请输入有效邮箱"));
        return;
    }
    emit verifyCodeRequested(email);
}

void FindPasswordDialog::onSubmitClicked()
{
    QString email = emailEdit_->text().trimmed();
    QString code = codeEdit_->text().trimmed();
    QString pwd = newPwdEdit_->text();
    if (email.isEmpty() || code.isEmpty() || pwd.isEmpty()) {
        showMessage(QStringLiteral("请填写完整信息"));
        return;
    }
    if (pwd != confirmPwdEdit_->text()) {
        showMessage(QStringLiteral("两次密码不一致"));
        return;
    }
    emit resetPwdRequested(email, code, pwd);
}

void FindPasswordDialog::showMessage(const QString &msg)
{
    statusLabel_->setText(msg);
}

void FindPasswordDialog::setBusy(bool busy)
{
    sendCodeBtn_->setDisabled(busy);
    submitBtn_->setDisabled(busy);
}
