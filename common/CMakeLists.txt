# ============================================================================
# common/CMakeLists.txt — 公共基础库构建脚本
# 产出静态库目标：AIWZQ_Common（名称固定，供 server / client 链接）
# 对应设计文档：00-项目概述.md（工程结构）、06-加密系统设计.md（依赖）
# 说明：common 为服务端 / 客户端共用基础库，纯 C++，不引入 Qt 依赖。
# ============================================================================

cmake_minimum_required(VERSION 3.15)
project(AIWZQ_Common LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------- 源文件 ----------------
set(AIWZQ_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/GomokuBoard.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Validator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Crypto.cpp
)

# AI 引擎（GomokuAI.cpp / Evaluator.cpp / MCTS.cpp）由 agent-ai 负责实现。
# 这三个文件一旦就绪即自动纳入本目标，未就绪时不阻塞 common 构建。
foreach(_ai_src GomokuAI.cpp Evaluator.cpp MCTS.cpp)
    set(_ai_path "${CMAKE_CURRENT_SOURCE_DIR}/src/${_ai_src}")
    if(EXISTS "${_ai_path}")
        list(APPEND AIWZQ_COMMON_SOURCES "${_ai_path}")
    endif()
endforeach()

add_library(AIWZQ_Common STATIC ${AIWZQ_COMMON_SOURCES})

target_include_directories(AIWZQ_Common PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(AIWZQ_Common PUBLIC cxx_std_14)

# ---------------- OpenSSL（必需）----------------
find_package(OpenSSL REQUIRED)
target_link_libraries(AIWZQ_Common PUBLIC OpenSSL::SSL OpenSSL::Crypto)

# ---------------- Argon2（密码哈希）----------------
# 缺失时给出提示：hashPassword / verifyPassword 需要 argon2.h 与库才能链接。
find_path(AIWZQ_ARGON2_INCLUDE_DIR NAMES argon2.h)
find_library(AIWZQ_ARGON2_LIBRARY NAMES argon2 argon2id libargon2)
if(AIWZQ_ARGON2_INCLUDE_DIR AND AIWZQ_ARGON2_LIBRARY)
    target_include_directories(AIWZQ_Common PRIVATE ${AIWZQ_ARGON2_INCLUDE_DIR})
    target_link_libraries(AIWZQ_Common PUBLIC ${AIWZQ_ARGON2_LIBRARY})
    message(STATUS "AIWZQ_Common: argon2 found at ${AIWZQ_ARGON2_LIBRARY}")
else()
    message(WARNING "AIWZQ_Common: argon2 not found; hashPassword/verifyPassword need argon2 headers+library at build time.")
endif()

# ---------------- 线程（AI 的 MCTS 多线程 std::thread 使用）----------------
find_package(Threads QUIET)
if(Threads_FOUND)
    target_link_libraries(AIWZQ_Common PUBLIC Threads::Threads)
endif()

# ---------------- nlohmann/json（Protocol.h header-only，可选）----------------
find_package(nlohmann_json QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(AIWZQ_Common PUBLIC nlohmann_json::nlohmann_json)
endif()
