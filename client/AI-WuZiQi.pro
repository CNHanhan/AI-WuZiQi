# AI-WuZiQi 客户端 qmake 工程（备选；CMake 优先）
QT += core gui network
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11
TARGET = AI-WuZiQi
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    src/main.cpp \
    src/TcpClient.cpp \
    src/ProtocolCodec.cpp \
    src/CryptoManager.cpp \
    src/CertificatePinner.cpp \
    src/SecureStorage.cpp \
    src/AIManager.cpp \
    src/GameController.cpp \
    ui/GomokuBoardWidget.cpp \
    ui/LoginWindow.cpp \
    ui/MainWindow.cpp \
    ui/RoomWindow.cpp \
    ui/GameWindow.cpp \
    ui/ShopWindow.cpp \
    ui/RankDialog.cpp

HEADERS += \
    src/TcpClient.h \
    src/ProtocolCodec.h \
    src/CryptoManager.h \
    src/CertificatePinner.h \
    src/SecureStorage.h \
    src/AIManager.h \
    src/GameController.h \
    ui/GomokuBoardWidget.h \
    ui/LoginWindow.h \
    ui/MainWindow.h \
    ui/RoomWindow.h \
    ui/GameWindow.h \
    ui/ShopWindow.h \
    ui/RankDialog.h

RESOURCES += resources/resources.qrc

INCLUDEPATH += $$PWD/../common/include
DEPENDPATH += $$PWD/../common/include

# 链接公共静态库（由 common 工程产出）与 OpenSSL / Advapi32
LIBS += -L$$PWD/../common -lAIWZQ_Common -lssl -lcrypto
win32: LIBS += -lAdvapi32
unix:!macx: LIBS += -lpthread
