// LoginWindow UI 自动化测试：验证用户名/密码/邮箱输入框的尺寸与可输入性
#include <QApplication>
#include <QLineEdit>
#include <QDebug>
#include <QKeyEvent>
#include "LoginWindow.h"

static bool testField(QWidget* w, const char* fieldName, const QString& label) {
    QList<QLineEdit*> edits = w->findChildren<QLineEdit*>();
    QLineEdit* target = nullptr;
    for (QLineEdit* e : edits) {
        if (e->placeholderText().contains(label) || e->objectName() == fieldName) {
            target = e; break;
        }
    }
    if (!target) {
        qWarning() << "[FAIL]" << fieldName << "not found";
        return false;
    }
    QRect g = target->geometry();
    qInfo() << fieldName << "geom=" << g.width() << "x" << g.height()
            << "enabled=" << target->isEnabled()
            << "readOnly=" << target->isReadOnly()
            << "visible=" << target->isVisible()
            << "maxLen=" << target->maxLength();

    // 用 sendEvent 逐字符发送 ASCII（绕过 QTest 平台插件兼容问题）
    target->clear();
    target->setFocus();
    QString typed = "abc123";
    for (int i = 0; i < typed.size(); ++i) {
        QString ch(typed[i]);
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, ch);
        QCoreApplication::sendEvent(target, &press);
    }
    bool ok = (target->text() == typed);
    qInfo() << (ok ? "[PASS]" : "[FAIL]") << fieldName << "text=" << target->text();
    return ok;
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    LoginWindow w;
    w.show();
    qInfo() << "window size=" << w.width() << "x" << w.height();

    bool all = true;
    all &= testField(&w, "usernameEdit_", QStringLiteral("用户名"));
    all &= testField(&w, "passwordEdit_", QStringLiteral("密码"));
    all &= testField(&w, "emailEdit_", QStringLiteral("邮箱"));
    qInfo() << "=== UI" << (all ? "ALL PASS" : "SOME FAILED") << "===";
    return all ? 0 : 1;
}
