cmake_minimum_required(VERSION 3.16)
project(AIWZQ_Server LANGUAGES CXX)

# ---------- 语言标准 ----------
set(CMAKE_CXX_STANDARD 17)          # 业务层使用 std::optional，需 C++17（common 的 cxx_std_14 向下兼容）
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------- 公共基础库 AIWZQ_Common ----------
# 约定：仓库根目录下 common/CMakeLists.txt 定义 AIWZQ_Common（含 include 路径、
# OpenSSL / argon2 / nlohmann_json / Threads 依赖）。
get_filename_component(AIWZQ_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
if(NOT TARGET AIWZQ_Common AND EXISTS "${AIWZQ_ROOT}/common/CMakeLists.txt")
    add_subdirectory(${AIWZQ_ROOT}/common ${CMAKE_BINARY_DIR}/aiwzq_common)
endif()
if(NOT TARGET AIWZQ_Common)
    # 兜底：common 尚未就绪时提供 INTERFACE target（README 注释说明）
    add_library(AIWZQ_Common INTERFACE)
    target_include_directories(AIWZQ_Common INTERFACE ${AIWZQ_ROOT}/common/include)
    find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)
    if(NLOHMANN_JSON_INCLUDE_DIR)
        target_include_directories(AIWZQ_Common INTERFACE ${NLOHMANN_JSON_INCLUDE_DIR})
    endif()
    message(WARNING "AIWZQ_Common static lib not found; using INTERFACE fallback. "
                    "Crypto.cpp/Argon2 must be linked elsewhere for hashPassword.")
endif()

# ---------- biz 数据库依赖（biz agent 提供） ----------
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BizDeps.cmake OPTIONAL)
if(NOT TARGET aiwzq_biz_deps)
    add_library(aiwzq_biz_deps INTERFACE)
    message(WARNING "cmake/BizDeps.cmake not found; MySQL/hiredis may be unresolved at link time.")
endif()

# ---------- OpenSSL / spdlog / Threads ----------
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    find_path(SPDLOG_INCLUDE_DIR spdlog/spdlog.h)
    find_library(SPDLOG_LIBRARY NAMES spdlog spdlogd)
    add_library(spdlog::spdlog INTERFACE IMPORTED)
    if(SPDLOG_INCLUDE_DIR)
        set_target_properties(spdlog::spdlog PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${SPDLOG_INCLUDE_DIR}")
    endif()
    if(SPDLOG_LIBRARY)
        set_target_properties(spdlog::spdlog PROPERTIES
            INTERFACE_LINK_LIBRARIES "${SPDLOG_LIBRARY}")
    else()
        set_target_properties(spdlog::spdlog PROPERTIES
            INTERFACE_COMPILE_DEFINITIONS "SPDLOG_HEADER_ONLY")
    endif()
    message(STATUS "spdlog: find_library fallback (include=${SPDLOG_INCLUDE_DIR}, lib=${SPDLOG_LIBRARY})")
endif()

# ---------- 可执行文件 ----------
# 网络层源文件（本 agent 负责）
set(AIWZQ_NET_SOURCES
    src/main.cpp
    src/TcpServer.cpp
    src/EventLoop.cpp
)
# 业务层源文件（biz agent 产出；TcpConnection 为 header-only）
file(GLOB AIWZQ_BIZ_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Database.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/RedisClient.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SessionManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/RateLimiter.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SecurityLogger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Room.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/RoomManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BizHandler.cpp
)

add_executable(AIWZQ_Server ${AIWZQ_NET_SOURCES} ${AIWZQ_BIZ_SOURCES})
target_include_directories(AIWZQ_Server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${AIWZQ_ROOT}/common/include
)

target_link_libraries(AIWZQ_Server PRIVATE
    AIWZQ_Common
    aiwzq_biz_deps
    OpenSSL::SSL
    OpenSSL::Crypto
    spdlog::spdlog
    Threads::Threads
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(AIWZQ_Server PRIVATE OS_LINUX)
    target_compile_options(AIWZQ_Server PRIVATE -Wall -Wextra)
endif()

if(WIN32)
    # Winsock2：select/recv/send/closesocket/WSAStartup 全部来自 ws2_32。
    target_link_libraries(AIWZQ_Server PRIVATE ws2_32)
endif()

message(STATUS "AIWZQ_Server configured: ${CMAKE_CXX_STANDARD}, openssl=${OPENSSL_VERSION}")
