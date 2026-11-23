// 终验：模拟完整 GameController.centerAndShow 流程，验证 LoginWindow 最终位置居中
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QDebug>
#include <QCoreApplication>
#include <QTimer>
#include "LoginWindow.h"

static void doCenter(QWidget* w) {
    QScreen* screen = w->screen() ? w->screen() : QGuiApplication::primaryScreen();
    if (!screen) return;
    const QRect avail = screen->availableGeometry();
    const QRect frame = w->frameGeometry();
    const int fw = frame.isEmpty() ? w->width() : frame.width();
    const int fh = frame.isEmpty() ? w->height() : frame.height();
    w->move(avail.center().x() - fw/2, avail.center().y() - fh/2);
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
#endif
    LoginWindow w;
    w.show(); w.raise(); w.activateWindow();
    QCoreApplication::processEvents();
    doCenter(&w);
    QTimer::singleShot(0, &w, [&w](){ doCenter(&w); });
    QTimer::singleShot(100, &app, [&](){
        QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
        QRect g = w.frameGeometry();
        int dx = qAbs(g.center().x()-avail.center().x());
        int dy = qAbs(g.center().y()-avail.center().y());
        qInfo() << "screen=" << avail << "window=" << g;
        qInfo() << "diff=(" << dx << "," << dy << ")" << (dx<=3&&dy<=3?"[PASS] 居中":"[FAIL] 未居中");
        app.exit(dx<=3&&dy<=3?0:1);
    });
    return app.exec();
}
