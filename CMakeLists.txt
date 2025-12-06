# CMakeLists.txt for Async Logger Project
cmake_minimum_required(VERSION 3.10)
project(AsyncLogger VERSION 1.0.0 LANGUAGES CXX)

# C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
add_compile_options(-Wall -Wextra -pthread)
add_compile_options("$<$<NOT:$<CONFIG:Debug>>:-O2>")

# 源文件
set(LOGGER_SOURCES
    logger.cpp
)

set(LOGGER_HEADERS
    logger.h
)

# 创建静态库
add_library(asynclogger STATIC ${LOGGER_SOURCES} ${LOGGER_HEADERS})

# 设置包含目录
target_include_directories(asynclogger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 链接线程库
find_package(Threads REQUIRED)
target_link_libraries(asynclogger PUBLIC Threads::Threads)

# 示例程序
add_executable(logger_test examples/example.cpp)
target_link_libraries(logger_test asynclogger)

# 基准测试程序
add_executable(logger_benchmark tests/benchmark.cpp)
target_link_libraries(logger_benchmark asynclogger)

# 单元测试程序
add_executable(logger_unit_test tests/unit_test.cpp)
target_link_libraries(logger_unit_test asynclogger)

# 安装规则
install(TARGETS asynclogger
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES ${LOGGER_HEADERS}
    DESTINATION include/tinymq/common
)

install(TARGETS logger_test logger_benchmark logger_unit_test
    RUNTIME DESTINATION bin
)

# 打印配置信息
message(STATUS "========================================")
message(STATUS "  AsyncLogger Configuration")
message(STATUS "========================================")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "========================================")
