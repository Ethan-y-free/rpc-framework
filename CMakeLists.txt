cmake_minimum_required(VERSION 3.16)
project(rpc_framework LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ==========================================================
# Build type
# ==========================================================
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Debug | Release | RelWithDebInfo" FORCE)
endif()
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# ==========================================================
# 核心库 net：自动收集 src/ 下全部 .cc / .cpp
#
# 增量写码规则：
#   每新建一个核心模块 .cc（如 Timer.cc、TimerQueue.cc），
#   下次构建自动收进 net 库，不用改本文件。
#   CONFIGURE_DEPENDS = VS 每次构建前 / Makefile 会检测新文件。
#
# 自动排除（这些作为独立可执行文件，不进库）：
#   demo_*.*       演示/测试入口
#   *_test.cc|*.cpp 单元测试
#   main.cc|main.cpp 备用入口
# ==========================================================
file(GLOB NET_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(FILTER NET_SOURCES EXCLUDE REGEX "demo_|_test\\.(cc|cpp)$|(^|/)main\\.(cc|cpp)$")

add_library(net STATIC ${NET_SOURCES})

# net 库导出自己的头目录：链接它的 demo（demo_echo_tcp 等）自动获得 src/ 的 include 路径
target_include_directories(net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# _GNU_SOURCE：eventfd / timerfd / epoll / accept4 等 Linux 特有 API 的声明
# 在 glibc 里受 __USE_GNU 保护，严格 c++17（CMAKE_CXX_EXTENSIONS OFF）下
# 必须显式开启，否则"未声明"。muduo 同款做法。
target_compile_definitions(net PUBLIC _GNU_SOURCE)

find_package(Threads REQUIRED)
target_link_libraries(net PUBLIC Threads::Threads)

# ==========================================================
# 演示可执行文件（文件存在才构建，写完即自动启用）
# ==========================================================

# ---- 多线程 echo server：跑通网络库核心（one loop per thread）----
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demo_echo.cpp")
    add_executable(demo_echo demo_echo.cpp)
    target_link_libraries(demo_echo PRIVATE net)
endif()

# ---- 业务层验证：基于 TcpServer 的 echo server ----
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demo_echo_tcp.cpp")
    add_executable(demo_echo_tcp demo_echo_tcp.cpp)
    target_link_libraries(demo_echo_tcp PRIVATE net)
endif()

# ---- 后续 demo 模板（复制后改名字即可）----
# if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/demo_http.cpp")
#     add_executable(demo_http demo_http.cpp)
#     target_link_libraries(demo_http PRIVATE net)
# endif()
