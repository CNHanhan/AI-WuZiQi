# BizDeps.cmake — 服务端业务层第三方依赖定位
# 对应设计文档：01-服务端设计.md 3.1（编译）；服务端设计.md 2.7/2.8
#
# 用法（在 server/CMakeLists.txt 中）：
#   include(cmake/BizDeps.cmake)
#   add_executable(ai-wuziqi-server ... 各 .cpp ...)
#   target_link_libraries(ai-wuziqi-server PRIVATE aiwzq_biz_deps)
#   # 若网络层另有 OpenSSL/Threads 依赖，照常追加即可。
#
# 说明：本机若未安装 MySQL/hiredis 开发库，find_path/find_library 返回 NOTFOUND，
# 这里仅做兜底探测并给出提示，不强制 FATAL_ERROR——源码按 mysql C API / hiredis
# 正确调用编写，装上开发库后重新 cmake 即可自动接续。

# ---- MySQL (mysql C API) ----
find_path(MYSQL_INCLUDE_DIR mysql.h
    PATHS
        /usr/include/mysql
        /usr/local/include/mysql
        /usr/local/mysql/include
        "C:/Program Files/MySQL/MySQL Server 8.0/include"
    PATH_SUFFIXES mysql
)
find_library(MYSQL_LIBRARY
    NAMES mysqlclient mysqlclient_r libmysql mysql
    PATHS /usr/lib /usr/lib64 /usr/local/lib /usr/local/mysql/lib
          "C:/Program Files/MySQL/MySQL Server 8.0/lib"
)

# ---- hiredis ----
find_path(HIREDIS_INCLUDE_DIR hiredis.h
    PATHS /usr/include /usr/local/include
    PATH_SUFFIXES hiredis
)
find_library(HIREDIS_LIBRARY
    NAMES hiredis
    PATHS /usr/lib /usr/lib64 /usr/local/lib
)

# ---- 汇总为 INTERFACE target ----
add_library(aiwzq_biz_deps INTERFACE)

if(MYSQL_INCLUDE_DIR)
    target_include_directories(aiwzq_biz_deps INTERFACE ${MYSQL_INCLUDE_DIR})
endif()
if(HIREDIS_INCLUDE_DIR)
    target_include_directories(aiwzq_biz_deps INTERFACE ${HIREDIS_INCLUDE_DIR})
endif()

# 头文件目录由调用方按项目结构自行添加（server/include, common/include），
# 这里只负责第三方库，避免与网络层 CMake 设置重复：
#   target_include_directories(<target> PRIVATE ${CMAKE_SOURCE_DIR}/../common/include ${CMAKE_SOURCE_DIR}/include)

set(_biz_missing "")
if(NOT MYSQL_INCLUDE_DIR)
    list(APPEND _biz_missing "MySQL headers (mysql.h)")
endif()
if(NOT MYSQL_LIBRARY)
    list(APPEND _biz_missing "MySQL library (libmysqlclient/libmysql)")
endif()
if(NOT HIREDIS_INCLUDE_DIR)
    list(APPEND _biz_missing "hiredis headers (hiredis.h)")
endif()
if(NOT HIREDIS_LIBRARY)
    list(APPEND _biz_missing "hiredis library (libhiredis)")
endif()

if(_biz_missing)
    message(STATUS "[aiwzq_biz_deps] 未找到第三方依赖：${_biz_missing}")
    message(STATUS "[aiwzq_biz_deps] 业务源码已按 mysql C API / hiredis 正确编写，安装开发库后重新运行 cmake 即可。")
else()
    target_link_libraries(aiwzq_biz_deps INTERFACE ${MYSQL_LIBRARY} ${HIREDIS_LIBRARY})
    message(STATUS "[aiwzq_biz_deps] MySQL: ${MYSQL_LIBRARY} | hiredis: ${HIREDIS_LIBRARY}")
endif()

unset(_biz_missing)
