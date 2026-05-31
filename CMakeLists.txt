cmake_minimum_required(VERSION 3.14)
project(netdisk C)

# ============================================================
# 编译选项
# ============================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)          # 生成 compile_commands.json（LSP 友好）

add_compile_definitions(_GNU_SOURCE OPENSSL_SUPPRESS_DEPRECATED)

set(CMAKE_C_FLAGS_DEBUG   "-g -O0 -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")

# ============================================================
# 外部依赖
# ============================================================
find_package(OpenSSL REQUIRED)
find_package(ZLIB    REQUIRED)
find_package(Threads REQUIRED)

# MySQL C API（Ubuntu/Debian: libmysqlclient-dev）
find_library(MYSQL_CLIENT_LIB mysqlclient REQUIRED)
find_path(MYSQL_INCLUDE_DIR mysql/mysql.h REQUIRED)

# ============================================================
# 模块：queue — 线程安全阻塞队列
# ============================================================
add_library(netdisk-queue STATIC
    queue/queue.c
)
target_include_directories(netdisk-queue PUBLIC
    ${CMAKE_SOURCE_DIR}/queue
)
target_link_libraries(netdisk-queue PUBLIC Threads::Threads)

# ============================================================
# 模块：sql — MySQL 数据库封装
# ============================================================
add_library(netdisk-sql STATIC
    sql/sql.c
)
target_include_directories(netdisk-sql PUBLIC
    ${CMAKE_SOURCE_DIR}/sql
    ${MYSQL_INCLUDE_DIR}
)
target_link_libraries(netdisk-sql PUBLIC ${MYSQL_CLIENT_LIB})

# ============================================================
# 自动收集 src/ 下已有源文件（新增 .c 文件时无需改 CMake）
# ============================================================
file(GLOB SERVER_SOURCES CONFIGURE_DEPENDS
    src/*.c
)

# 排除开发中尚未实现的占位文件（empty 文件会被检测）
# 当文件有实际内容后，会自动纳入编译

# ============================================================
# 可执行目标：服务端
# ============================================================
add_executable(netdisk-server ${SERVER_SOURCES})
target_include_directories(netdisk-server PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/queue
    ${CMAKE_SOURCE_DIR}/sql
    ${CMAKE_SOURCE_DIR}/shared
    ${OPENSSL_INCLUDE_DIR}
    ${ZLIB_INCLUDE_DIRS}
    ${MYSQL_INCLUDE_DIR}
)
target_link_libraries(netdisk-server PRIVATE
    netdisk-queue
    netdisk-sql
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
    Threads::Threads
    ${MYSQL_CLIENT_LIB}
)

# ============================================================
# 可执行目标：客户端（client/ 目录为空时跳过）
# ============================================================
file(GLOB CLIENT_SOURCES CONFIGURE_DEPENDS
    client/*.c
)

if(CLIENT_SOURCES)
    add_executable(netdisk-cli ${CLIENT_SOURCES})
    target_include_directories(netdisk-cli PRIVATE
        ${CMAKE_SOURCE_DIR}/client
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/shared
        ${OPENSSL_INCLUDE_DIR}
        ${ZLIB_INCLUDE_DIRS}
    )
    target_link_libraries(netdisk-cli PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
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
