// 诊断大厅聊天框：真实Windows键盘消息能否输入
#include <QApplication>
#include <QLineEdit>
#include <QDebug>
#include <QThread>
#include <windows.h>
#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    QCoreApplication::processEvents();
    QThread::msleep(100);
    QCoreApplication::processEvents();

    QList<QLineEdit*> edits = w.findChildren<QLineEdit*>();
    QLineEdit* chat=nullptr;
    for (QLineEdit* e: edits)
        if (e->placeholderText().contains(QStringLiteral("聊天"))) { chat=e; break; }
    if (!chat) { qWarning()<<"chat not found"; return 2; }

    qInfo()<<"activeWindow="<<(QApplication::activeWindow()?"yes":"no")
           <<"focus="<<(QApplication::focusWidget()?QApplication::focusWidget()->metaObject()->className():"null")
           <<"inputMethodEnabled="<<chat->testAttribute(Qt::WA_InputMethodEnabled);

    HWND hwnd = reinterpret_cast<HWND>(w.winId());
    chat->setFocus();
    const char* text="abc123";
    for (const char* p=text; *p; ++p) PostMessage(hwnd, WM_CHAR, (WPARAM)(unsigned char)*p, 0);
    QThread::msleep(150);
    QCoreApplication::processEvents(); QCoreApplication::processEvents();
    qInfo()<<"after WM_CHAR text="<<chat->text();
    bool ok=(chat->text()=="abc123");
    qInfo()<<(ok?"[PASS] 键盘消息可达聊天框":"[FAIL] 键盘消息被吞");
    return ok?0:1;
}
