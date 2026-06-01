cmake_minimum_required(VERSION 3.14)
project(netdisk C)

# ============================================================
# 编译选项
# ============================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions(_GNU_SOURCE)

set(CMAKE_C_FLAGS_DEBUG   "-g -O0 -Wall -Wextra")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

# ============================================================
# 外部依赖
# ============================================================
find_package(ZLIB    REQUIRED)
find_package(Threads REQUIRED)

# MySQL C API（Ubuntu/Debian: libmysqlclient-dev）
find_library(MYSQL_CLIENT_LIB mysqlclient REQUIRED)
find_path(MYSQL_INCLUDE_DIR mysql/mysql.h REQUIRED)

# ============================================================
# 自动收集源文件
# ============================================================

# 服务端：src/ + common/ 下所有 .c
file(GLOB_RECURSE SERVER_SOURCES CONFIGURE_DEPENDS
    src/*.c
    common/*.c
)

# 客户端：client/ 下所有 .c + 共享模块
file(GLOB_RECURSE CLIENT_SOURCES CONFIGURE_DEPENDS
    client/*.c
    common/ini_parser.c
    common/proto/tlv.c
    common/utils/utils.c
)

# ============================================================
# 可执行目标：服务端 netdisk-server
# ============================================================
add_executable(netdisk-server ${SERVER_SOURCES})
target_include_directories(netdisk-server PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/common
    ${MYSQL_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
)
target_link_libraries(netdisk-server PRIVATE
    ${MYSQL_CLIENT_LIB}
    ZLIB::ZLIB
    Threads::Threads
)

# ============================================================
# 可执行目标：客户端 netdisk-cli
# ============================================================
if(CLIENT_SOURCES)
    add_executable(netdisk-cli ${CLIENT_SOURCES})
    target_include_directories(netdisk-cli PRIVATE
        ${CMAKE_SOURCE_DIR}/client
        ${CMAKE_SOURCE_DIR}/common
        ${ZLIB_INCLUDE_DIRS}
    )
    target_link_libraries(netdisk-cli PRIVATE
        ZLIB::ZLIB
        Threads::Threads
    )
else()
    message(STATUS "跳过客户端构建：client/ 目录下暂无 .c 源文件")
endif()

# ============================================================
# 总结信息
# ============================================================
message(STATUS "========================================")
message(STATUS "  项目: NetDisk 高性能网盘服务")
message(STATUS "  构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C 标准:   C11")
message(STATUS "========================================")
