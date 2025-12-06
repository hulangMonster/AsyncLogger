#!/bin/bash
# Linux 构建脚本 - 编译和运行异步日志系统
# 所有产物（.o/.a/可执行文件/日志）统一输出到 output/ 目录

set -e

echo "========================================"
echo "  Async Logger Build Script"
echo "========================================"
echo ""

# 检测编译器
if command -v g++ &> /dev/null; then
    CXX=g++
elif command -v clang++ &> /dev/null; then
    CXX=clang++
else
    echo "Error: No C++ compiler found!"
    echo "Please install g++ or clang++"
    exit 1
fi

echo "Using compiler: $CXX"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -pthread -I."

# 函数定义
build_example() {
    echo "Building example program..."
    mkdir -p output
    $CXX $CXXFLAGS logger.cpp examples/example.cpp -o output/logger_test
    echo "Example program built successfully: output/logger_test"
}

build_benchmark() {
    echo "Building benchmark program..."
    mkdir -p output
    $CXX $CXXFLAGS logger.cpp tests/benchmark.cpp -o output/logger_benchmark
    echo "Benchmark program built successfully: output/logger_benchmark"
}

build_unit_test() {
    echo "Building unit test program..."
    mkdir -p output
    $CXX $CXXFLAGS logger.cpp tests/unit_test.cpp -o output/logger_unit_test
    echo "Unit test program built successfully: output/logger_unit_test"
}

build_all() {
    echo "Building all targets..."
    build_example
    echo ""
    build_benchmark
    echo ""
    build_unit_test
    echo ""
    echo "Build complete!"
}

run_example() {
    if [ ! -f output/logger_test ]; then
        echo "logger_test not found, building..."
        build_example
        echo ""
    fi
    echo "Running examples..."
    mkdir -p output
    cd output
    ./logger_test
    cd ..
}

run_benchmark() {
    if [ ! -f output/logger_benchmark ]; then
        echo "logger_benchmark not found, building..."
        build_benchmark
        echo ""
    fi
    echo "Running benchmarks..."
    mkdir -p output
    cd output
    ./logger_benchmark
    cd ..
}

run_unit_test() {
    if [ ! -f output/logger_unit_test ]; then
        echo "logger_unit_test not found, building..."
        build_unit_test
        echo ""
    fi
    echo "Running unit tests..."
    mkdir -p output
    cd output
    ./logger_unit_test
    cd ..
}

clean() {
    echo "Cleaning build artifacts..."
    rm -f *.o logger_test logger_benchmark logger_unit_test
    rm -f *.log *.log.*
    rm -rf output
    echo "Clean complete!"
}

show_help() {
    echo "Usage: ./build.sh [command]"
    echo ""
    echo "Commands:"
    echo "  (none)     - Build all targets"
    echo "  example    - Build example program only"
    echo "  benchmark  - Build benchmark program only"
    echo "  test       - Build unit test program only"
    echo "  run        - Build and run examples"
    echo "  bench      - Build and run benchmarks"
    echo "  runtest    - Build and run unit tests"
    echo "  clean      - Remove build artifacts (including output/)"
    echo "  help       - Show this help message"
    echo ""
}

# 主逻辑
case "${1:-all}" in
    all)
        build_all
        ;;
    example)
        build_example
        ;;
    benchmark)
        build_benchmark
        ;;
    test)
        build_unit_test
        ;;
    run)
        run_example
        ;;
    bench)
        run_benchmark
        ;;
    runtest)
        run_unit_test
        ;;
    clean)
        clean
        ;;
    help)
        show_help
        ;;
    *)
        echo "Unknown command: $1"
        show_help
        exit 1
        ;;
esac

echo ""
echo "Done!"
