# Makefile for Async Logger Project
# 面向 Linux（g++/clang++），所有产物统一输出到 output/ 目录

# 编译器
CXX := g++
RM := rm -f
RMDIR := rm -rf
EXE_EXT :=
MKDIR := mkdir -p

# 编译选项
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -pthread -I.
LDFLAGS := -pthread

# 输出目录（编译产物 + 运行日志统一在此）
BUILD_DIR := output

# 目标文件（实际路径在 output/ 下）
TARGET_TEST := logger_test$(EXE_EXT)
TARGET_BENCH := logger_benchmark$(EXE_EXT)
TARGET_UT := logger_unit_test$(EXE_EXT)
TARGET_LIB := libasynclogger.a

# 源文件
LOGGER_SRC := logger.cpp
LOGGER_OBJ := $(BUILD_DIR)/logger.o
EXAMPLE_SRC := examples/example.cpp
BENCHMARK_SRC := tests/benchmark.cpp
UNIT_TEST_SRC := tests/unit_test.cpp

# 头文件
HEADERS := logger.h

# 默认目标
.PHONY: all
all: $(BUILD_DIR)/$(TARGET_TEST) $(BUILD_DIR)/$(TARGET_BENCH) $(BUILD_DIR)/$(TARGET_UT)

# 编译静态库
$(BUILD_DIR)/$(TARGET_LIB): $(LOGGER_OBJ)
	@echo "Creating static library $@..."
	ar rcs $@ $^

# 编译日志系统对象文件
$(LOGGER_OBJ): $(LOGGER_SRC) $(HEADERS)
	@$(MKDIR) $(BUILD_DIR)
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 编译示例程序
$(BUILD_DIR)/$(TARGET_TEST): $(EXAMPLE_SRC) $(BUILD_DIR)/$(TARGET_LIB)
	@echo "Building example program $@..."
	$(CXX) $(CXXFLAGS) $< $(BUILD_DIR)/$(TARGET_LIB) $(LDFLAGS) -o $@

# 编译基准测试程序
$(BUILD_DIR)/$(TARGET_BENCH): $(BENCHMARK_SRC) $(BUILD_DIR)/$(TARGET_LIB)
	@echo "Building benchmark program $@..."
	$(CXX) $(CXXFLAGS) $< $(BUILD_DIR)/$(TARGET_LIB) $(LDFLAGS) -o $@

# 编译单元测试程序
$(BUILD_DIR)/$(TARGET_UT): $(UNIT_TEST_SRC) $(BUILD_DIR)/$(TARGET_LIB)
	@echo "Building unit test program $@..."
	$(CXX) $(CXXFLAGS) $< $(BUILD_DIR)/$(TARGET_LIB) $(LDFLAGS) -o $@

# 运行示例
.PHONY: run
run: $(BUILD_DIR)/$(TARGET_TEST)
	@echo "Running examples..."
	cd output && ./$(TARGET_TEST) && cd ..

# 运行基准测试
.PHONY: benchmark
benchmark: $(BUILD_DIR)/$(TARGET_BENCH)
	@echo "Running benchmarks..."
	cd output && ./$(TARGET_BENCH) && cd ..

# 运行单元测试
.PHONY: test
test: $(BUILD_DIR)/$(TARGET_UT)
	@echo "Running unit tests..."
	cd output && ./$(TARGET_UT) && cd ..

# 运行单个示例
.PHONY: run-basic run-multi run-file run-stress run-filter
run-basic: $(BUILD_DIR)/$(TARGET_TEST)
	cd output && ./$(TARGET_TEST) 1 && cd ..

run-multi: $(BUILD_DIR)/$(TARGET_TEST)
	cd output && ./$(TARGET_TEST) 2 && cd ..

run-file: $(BUILD_DIR)/$(TARGET_TEST)
	cd output && ./$(TARGET_TEST) 3 && cd ..

run-stress: $(BUILD_DIR)/$(TARGET_TEST)
	cd output && ./$(TARGET_TEST) 4 && cd ..

run-filter: $(BUILD_DIR)/$(TARGET_TEST)
	cd output && ./$(TARGET_TEST) 5 && cd ..

# 运行单个基准测试
.PHONY: bench-single bench-multi bench-length bench-stress bench-filter bench-frontend
bench-single: $(BUILD_DIR)/$(TARGET_BENCH)
	cd output && ./$(TARGET_BENCH) 1 && cd ..

bench-multi: $(BUILD_DIR)/$(TARGET_BENCH)
	cd output && ./$(TARGET_BENCH) 2 && cd ..

bench-length: $(BUILD_DIR)/$(TARGET_BENCH)
	cd output && ./$(TARGET_BENCH) 3 && cd ..

bench-stress: $(BUILD_DIR)/$(TARGET_BENCH)
	cd output && ./$(TARGET_BENCH) 4 && cd ..

bench-filter: $(BUILD_DIR)/$(TARGET_BENCH)
	cd output && ./$(TARGET_BENCH) 5 && cd ..

bench-frontend: $(BUILD_DIR)/$(TARGET_BENCH)
	cd output && ./$(TARGET_BENCH) 6 && cd ..

# 清理
.PHONY: clean
clean:
	@echo "Cleaning up..."
	-$(RMDIR) $(BUILD_DIR)
	-$(RM) *.log *.log.*

# 清理所有生成文件
.PHONY: distclean
distclean: clean
	@echo "Deep cleaning..."
	-$(RM) *.o *.a

# 安装
.PHONY: install
install: $(BUILD_DIR)/$(TARGET_LIB) $(HEADERS)
	@echo "Installing library and headers..."
	install -d $(DESTDIR)/usr/local/lib
	install -d $(DESTDIR)/usr/local/include/tinymq/common
	install -m 644 $(BUILD_DIR)/$(TARGET_LIB) $(DESTDIR)/usr/local/lib/
	install -m 644 $(HEADERS) $(DESTDIR)/usr/local/include/tinymq/common/
	@echo "Installation complete"

# 帮助信息
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  all          - Build all targets (default)"
	@echo "  clean        - Remove build artifacts (output/)"
	@echo "  distclean    - Remove all generated files"
	@echo "  run          - Run all examples"
	@echo "  benchmark    - Run all benchmarks"
	@echo "  test         - Run unit tests"
	@echo "  install      - Install library and headers"
	@echo ""
	@echo "Run specific examples:"
	@echo "  run-basic    - Basic usage example"
	@echo "  run-multi    - Multi-threaded example"
	@echo "  run-file     - File output & rolling example"
	@echo "  run-stress   - Stress test example"
	@echo "  run-filter   - Log level filtering example"
	@echo ""
	@echo "Run specific benchmarks:"
	@echo "  bench-single - Single thread throughput"
	@echo "  bench-multi  - Multi-threaded performance"
	@echo "  bench-length - Varying message lengths"
	@echo "  bench-stress - Stress test (max load)"
	@echo "  bench-filter - Log level filtering"
	@echo "  bench-frontend - Frontend-only throughput (no disk)"

# 显示编译配置
.PHONY: info
info:
	@echo "Build Configuration:"
	@echo "  CXX       = $(CXX)"
	@echo "  CXXFLAGS  = $(CXXFLAGS)"
	@echo "  LDFLAGS   = $(LDFLAGS)"
	@echo "  Platform  = $(shell uname -s 2>/dev/null || echo Linux)"
	@echo "  BuildDir  = $(BUILD_DIR)"
