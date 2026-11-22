# client_ui.pri — Qt UI 层源文件清单
# 在 client 主工程 .pro 中 include 本文件即可：
#   include($$PWD/client_ui.pri)

HEADERS += \
    $$PWD/ui/LoginWindow.h \
    $$PWD/ui/MainWindow.h \
    $$PWD/ui/RoomWindow.h \
    $$PWD/ui/GameWindow.h \
    $$PWD/ui/GomokuBoardWidget.h \
    $$PWD/ui/ShopWindow.h \
    $$PWD/ui/RankDialog.h

SOURCES += \
    $$PWD/ui/LoginWindow.cpp \
    $$PWD/ui/MainWindow.cpp \
    $$PWD/ui/RoomWindow.cpp \
    $$PWD/ui/GameWindow.cpp \
    $$PWD/ui/GomokuBoardWidget.cpp \
    $$PWD/ui/ShopWindow.cpp \
    $$PWD/ui/RankDialog.cpp

# UI 层用代码手写布局，无 .ui 表单文件
# FORMS +=

RESOURCES += \
    $$PWD/resources/resources.qrc

# UI 引用的公共头文件目录（Types.h / GomokuBoard.h / GomokuAI.h 冻结接口）
INCLUDEPATH += $$PWD/../common/include
