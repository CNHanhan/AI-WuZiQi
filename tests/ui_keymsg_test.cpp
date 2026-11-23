// 决定性测试：用 Windows 原生 WM_CHAR 消息模拟真实键盘输入，
// 验证 QLineEdit 能否通过真实窗口消息通路接收字符。
#include <QApplication>
#include <QLineEdit>
#include <QDebug>
#include <QCoreApplication>
#include <QThread>
#ifdef _WIN32
#include <windows.h>
#endif
#include "LoginWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    LoginWindow w;
    w.show();
    QCoreApplication::processEvents();

    QList<QLineEdit*> edits = w.findChildren<QLineEdit*>();
    QLineEdit* user = nullptr;
    for (QLineEdit* e : edits)
        if (e->placeholderText().contains(QStringLiteral("用户名"))) { user = e; break; }
    if (!user) { qWarning() << "username edit not found"; return 2; }

    qInfo() << "activeWindow=" << (QApplication::activeWindow() ? "yes" : "no")
            << "focusWidget=" << (QApplication::focusWidget() ? QApplication::focusWidget()->metaObject()->className() : "null");

#ifdef _WIN32
    HWND hwnd = reinterpret_cast<HWND>(w.winId());
    const char* text = "abc123";
    for (const char* p = text; *p; ++p) {
        PostMessage(hwnd, WM_CHAR, (WPARAM)(unsigned char)*p, 0);
        QCoreApplication::processEvents();
    }
    // PostMessage 异步，等事件循环充分处理
    QThread::msleep(100);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    qInfo() << "after PostMessage WM_CHAR, username text=" << user->text();
#endif

    bool ok = (user->text() == "abc123");
    qInfo() << (ok ? "[PASS] 真实Windows键盘消息可输入" : "[FAIL] 真实Windows键盘消息无法输入");
    return ok ? 0 : 1;
}
