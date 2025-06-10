cmake_minimum_required(VERSION 3.10)
project(LinuxQQServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 启用更详细的编译信息与警告
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

# 查找 SQLite3
find_package(SQLite3 REQUIRED)

# 查找 OpenSSL（用于 SHA256）
find_package(OpenSSL REQUIRED)

# 包含头文件目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 源文件列表
set(SRC_FILES
    src/main.cpp
    src/Protocol.cpp
    src/ThreadPool.cpp
    src/DatabaseManager.cpp
    src/ChatServer.cpp
)

# 生成服务端可执行程序
add_executable(server ${SRC_FILES})

# 链接库：SQLite3、OpenSSL、pthread
target_link_libraries(server
    ${SQLite3_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread
)

# 打印链接信息（调试用）
message(STATUS "Using SQLite3: ${SQLite3_LIBRARIES}")
message(STATUS "Using OpenSSL: ${OPENSSL_LIBRARIES}")
