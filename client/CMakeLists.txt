# ============================================================================
# client/CMakeLists.txt — AI-WuZiQi Qt 客户端构建脚本
# Qt6 Widgets + Network + C++11，链接 AIWZQ_Common 与 OpenSSL。
# 用法（工程根）：
#   cmake -S . -B build
#   cmake --build build --target AI-WuZiQi_Client
# ============================================================================

cmake_minimum_required(VERSION 3.15)
project(AI-WuZiQi_Client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)          # common 为 C++14，客户端至少兼容 C++11
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)               # 自动处理 Q_OBJECT
set(CMAKE_AUTORCC ON)               # 自动处理 .qrc
set(CMAKE_AUTOUIC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# ---------------- 查找 Qt（优先 Qt6，回退 Qt5）----------------
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network)

# ---------------- OpenSSL ----------------
find_package(OpenSSL REQUIRED)

# ---------------- AIWZQ_Common（由 common/CMakeLists.txt 定义）----------------
# 若单独构建 client 目录而 common 尚未作为子目录，则尝试 add_subdirectory。
if(NOT TARGET AIWZQ_Common)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common ${CMAKE_CURRENT_BINARY_DIR}/common)
endif()

# ---------------- 源文件 ----------------
set(CLIENT_SOURCES
    src/main.cpp
    src/TcpClient.cpp
    src/ProtocolCodec.cpp
    src/CryptoManager.cpp
    src/CertificatePinner.cpp
    src/SecureStorage.cpp
    src/AIManager.cpp
    src/GameController.cpp

    # UI agent 产出
    ui/GomokuBoardWidget.cpp
    ui/LoginWindow.cpp
    ui/MainWindow.cpp
    ui/RoomWindow.cpp
    ui/GameWindow.cpp
    ui/ShopWindow.cpp
    ui/RankDialog.cpp
)

set(CLIENT_HEADERS
    src/TcpClient.h
    src/ProtocolCodec.h
    src/CryptoManager.h
    src/CertificatePinner.h
    src/SecureStorage.h
    src/AIManager.h
    src/GameController.h

    ui/GomokuBoardWidget.h
    ui/LoginWindow.h
    ui/MainWindow.h
    ui/RoomWindow.h
    ui/GameWindow.h
    ui/ShopWindow.h
    ui/RankDialog.h
)

set(CLIENT_RESOURCES
    resources/resources.qrc
)

add_executable(AI-WuZiQi_Client
    ${CLIENT_SOURCES}
    ${CLIENT_HEADERS}
    ${CLIENT_RESOURCES}
)

target_include_directories(AI-WuZiQi_Client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/include
)

target_link_libraries(AI-WuZiQi_Client PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    AIWZQ_Common
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Windows 下 SecureStorage 需要 Advapi32（CredWrite/CredRead/CredDelete）
# imm32 用于关闭输入框的中文输入法(IME)，避免中文输入状态吞掉键盘消息
if(WIN32)
    target_link_libraries(AI-WuZiQi_Client PRIVATE Advapi32 imm32)
endif()

# 输出名统一为 AI-WuZiQi
set_target_properties(AI-WuZiQi_Client PROPERTIES OUTPUT_NAME "AI-WuZiQi")
