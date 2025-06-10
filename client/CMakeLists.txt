cmake_minimum_required(VERSION 3.10)
project(LinuxQQClient)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 源文件
set(SRC_FILES
    src/TestClient.cpp
)

# 可执行文件
add_executable(TestClient ${SRC_FILES})

# 链接 pthread（使用线程/网络通信）
find_package(Threads REQUIRED)
target_link_libraries(TestClient Threads::Threads)

# 输出提示
message(STATUS "Building TestClient with headers in include/ and C++17 standard")
