// 验证窗口居中：模拟 GameController 的 centerAndShow 逻辑
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QMainWindow>
#include <QDebug>
#include <QCoreApplication>

static void centerRight(QWidget* w) {
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        const QRect frame = w->frameGeometry();
        const int wSize = frame.isEmpty() ? w->width() : frame.width();
        const int hSize = frame.isEmpty() ? w->height() : frame.height();
        w->move(avail.center().x() - wSize/2, avail.center().y() - hSize/2);
    }
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QMainWindow w;
    w.resize(420, 380);
    w.setWindowTitle("center test");
    w.show();
    QCoreApplication::processEvents();
    centerRight(&w);
    QCoreApplication::processEvents();

    QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
    QRect g = w.frameGeometry();
    QPoint wc = g.center(), sc = avail.center();
    int dx = qAbs(wc.x()-sc.x()), dy = qAbs(wc.y()-sc.y());
    qInfo() << "screen center=" << sc << "window frame=" << g << "window center=" << wc;
    qInfo() << "diff=(" << dx << "," << dy << ")";
    bool ok = dx <= 3 && dy <= 3;
    qInfo() << (ok?"[PASS] 窗口居中":"[FAIL] 窗口未居中");
    return ok ? 0 : 1;
}
