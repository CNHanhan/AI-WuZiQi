// AI-WuZiQi Qt 客户端入口
// 创建 QApplication + GameController，由 controller 负责窗口创建/切换与服务器连接。

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QIcon>

#include "GameController.h"

int main(int argc, char* argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
#endif
    QApplication app(argc, argv);
    // 应用图标：设置全局默认窗口图标，所有窗口/对话框自动继承（含标题栏与任务栏角标）
    app.setWindowIcon(QIcon(QStringLiteral(":/appicon.png")));
    QCoreApplication::setApplicationName(QStringLiteral("AI-WuZiQi"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("AI-WuZiQi"));

    GameController controller;
    controller.createWindows();     // 创建并连接全部窗口信号
    controller.setServer(QStringLiteral("127.0.0.1"), 8889);
    controller.start();             // 发起 TLS 连接（8889）
    controller.showAllWindows();    // 显示登录窗口

    // 把关键状态打印到 Qt 日志，便于联调
    QObject::connect(&controller, &GameController::statusMessage,
                     [](const QString& s) { qInfo().noquote() << "[AI-WuZiQi]" << s; });

    return app.exec();
}
