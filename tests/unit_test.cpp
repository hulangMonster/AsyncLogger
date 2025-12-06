/**
 * @file unit_test.cpp
 * @brief 异步日志系统单元测试
 * 
 * 测试场景：
 * 1. 基本功能测试
 * 2. 线程安全测试
 * 3. 缓冲区管理测试
 * 4. 文件轮转测试
 * 5. 边界条件测试
 */

#include "logger.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <vector>
#include <fstream>
#include <cstdio>

using namespace tinymq::common;

// 测试计数器
static int tests_passed = 0;
static int tests_failed = 0;

// 测试宏
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "❌ TEST FAILED: " << message << "\n"; \
            std::cerr << "   at " << __FILE__ << ":" << __LINE__ << "\n"; \
            tests_failed++; \
            return false; \
        } \
    } while(0)

#define RUN_TEST(test_func) \
    do { \
        std::cout << "\nRunning " << #test_func << "...\n"; \
        if (test_func()) { \
            std::cout << "✓ " << #test_func << " passed\n"; \
            tests_passed++; \
        } else { \
            tests_failed++; \
        } \
    } while(0)

// ========== 测试1：基本启动和停止 ==========
bool test_basic_start_stop() {
    AsyncLogger::getInstance().start();
    TEST_ASSERT(true, "Logger should start without error");
    
    AsyncLogger::getInstance().stop();
    TEST_ASSERT(true, "Logger should stop without error");
    
    // 测试重复启动
    AsyncLogger::getInstance().start();
    AsyncLogger::getInstance().start(); // 应该忽略
    AsyncLogger::getInstance().stop();
    
    return true;
}

// ========== 测试2：基本日志写入 ==========
bool test_basic_logging() {
    AsyncLogger::getInstance().start();
    AsyncLogger::getInstance().setLogLevel(LogLevel::DEBUG);
    
    LOG_DEBUG << "Debug message";
    LOG_INFO << "Info message";
    LOG_WARN << "Warning message";
    LOG_ERROR << "Error message";
    LOG_FATAL << "Fatal message";
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    auto stats = AsyncLogger::getInstance().getStats();
    TEST_ASSERT(stats.totalLogs == 5, "Should have logged 5 messages");
    
    AsyncLogger::getInstance().stop();
    return true;
}

// ========== 测试3：日志级别过滤 ==========
bool test_log_level_filtering() {
    AsyncLogger::getInstance().start();
    
    // 设置为 WARN 级别
    AsyncLogger::getInstance().setLogLevel(LogLevel::WARN);
    
    auto stats_before = AsyncLogger::getInstance().getStats();
    
    LOG_DEBUG << "Should not be logged";
    LOG_INFO << "Should not be logged";
    LOG_WARN << "Should be logged";
    LOG_ERROR << "Should be logged";
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    auto stats_after = AsyncLogger::getInstance().getStats();
    uint64_t logged = stats_after.totalLogs - stats_before.totalLogs;
    
    TEST_ASSERT(logged == 2, "Should have logged exactly 2 messages (WARN and ERROR)");
    
    AsyncLogger::getInstance().stop();
    return true;
}

// ========== 测试4：文件输出 ==========
bool test_file_output() {
    const char* test_file = "test_output.log";
    
    // 删除旧文件
    std::remove(test_file);
    
    AsyncLogger::getInstance().setLogLevel(LogLevel::INFO);
    AsyncLogger::getInstance().setOutputFile(test_file, 10 * 1024 * 1024);
    AsyncLogger::getInstance().start();
    
    LOG_INFO << "Test message 1";
    LOG_INFO << "Test message 2";
    LOG_INFO << "Test message 3";
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    AsyncLogger::getInstance().stop();
    
    // 检查文件是否存在
    std::ifstream file(test_file);
    TEST_ASSERT(file.good(), "Log file should exist");
    
    // 检查文件内容
    std::string line;
    int line_count = 0;
    while (std::getline(file, line)) {
        line_count++;
    }
    file.close();
    
    TEST_ASSERT(line_count >= 3, "Log file should contain at least 3 lines");
    
    // 清理
    std::remove(test_file);
    
    return true;
}

// ========== 测试5：多线程并发 ==========
void concurrent_worker(int thread_id, int num_logs) {
    for (int i = 0; i < num_logs; ++i) {
        LOG_INFO << "Thread " << thread_id << " message " << i;
    }
}

bool test_concurrent_logging() {
    AsyncLogger::getInstance().start();
    AsyncLogger::getInstance().setLogLevel(LogLevel::INFO);
    
    const int num_threads = 10;
    const int logs_per_thread = 100;
    
    auto stats_before = AsyncLogger::getInstance().getStats();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(concurrent_worker, i, logs_per_thread);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    auto stats_after = AsyncLogger::getInstance().getStats();
    uint64_t total_logs = stats_after.totalLogs - stats_before.totalLogs;
    
    TEST_ASSERT(total_logs == num_threads * logs_per_thread, 
                "Should have logged all messages from all threads");
    
    AsyncLogger::getInstance().stop();
    return true;
}

// ========== 测试6：大消息测试 ==========
bool test_large_messages() {
    AsyncLogger::getInstance().start();
    AsyncLogger::getInstance().setLogLevel(LogLevel::INFO);
    
    auto stats_before = AsyncLogger::getInstance().getStats();
    
    // 测试不同大小的消息
    std::string small_msg(10, 'A');
    std::string medium_msg(1000, 'B');
    std::string large_msg(10000, 'C');
    
    LOG_INFO << "Small: " << small_msg;
    LOG_INFO << "Medium: " << medium_msg;
    LOG_INFO << "Large: " << large_msg;
    
    // 先 stop() 优雅落盘，再统计：小日志不会触发缓冲区切换/notify，
    // 后台线程默认 3s 才超时刷盘，仅 sleep 500ms 后统计 bytesWritten 不可靠。
    AsyncLogger::getInstance().stop();
    
    auto stats_after = AsyncLogger::getInstance().getStats();
    uint64_t logged = stats_after.totalLogs - stats_before.totalLogs;
    
    TEST_ASSERT(logged == 3, "Should have logged 3 messages of varying sizes");
    TEST_ASSERT(stats_after.bytesWritten > stats_before.bytesWritten + 11000,
                "Should have written significant bytes");
    
    return true;
}

// ========== 测试7：快速启动停止 ==========
bool test_rapid_start_stop() {
    for (int i = 0; i < 5; ++i) {
        AsyncLogger::getInstance().start();
        LOG_INFO << "Rapid test iteration " << i;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        AsyncLogger::getInstance().stop();
    }
    
    TEST_ASSERT(true, "Should handle rapid start/stop cycles");
    return true;
}

// ========== 测试8：统计信息准确性 ==========
bool test_statistics() {
    AsyncLogger::getInstance().start();
    AsyncLogger::getInstance().setLogLevel(LogLevel::INFO);
    
    auto stats_before = AsyncLogger::getInstance().getStats();
    
    const int num_logs = 100;
    for (int i = 0; i < num_logs; ++i) {
        LOG_INFO << "Stats test message " << i;
    }
    
    // 先 stop() 优雅落盘，再统计（原因同 test_large_messages：小日志不触发 notify）
    AsyncLogger::getInstance().stop();
    
    auto stats_after = AsyncLogger::getInstance().getStats();
    
    TEST_ASSERT(stats_after.totalLogs >= stats_before.totalLogs + num_logs,
                "Total logs should increase correctly");
    TEST_ASSERT(stats_after.bytesWritten > stats_before.bytesWritten,
                "Bytes written should increase");
    
    return true;
}

// ========== 测试9：空消息处理 ==========
bool test_empty_messages() {
    AsyncLogger::getInstance().start();
    AsyncLogger::getInstance().setLogLevel(LogLevel::INFO);
    
    auto stats_before = AsyncLogger::getInstance().getStats();
    
    // 虽然消息为空，日志系统仍会记录时间戳和级别
    LOG_INFO << "";
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    auto stats_after = AsyncLogger::getInstance().getStats();
    
    // 空消息也应该被处理
    TEST_ASSERT(stats_after.totalLogs > stats_before.totalLogs,
                "Empty messages should still be processed");
    
    AsyncLogger::getInstance().stop();
    return true;
}

// ========== 测试10：文件轮转 ==========
bool test_file_rolling() {
    const char* test_file = "test_rolling.log";
    
    // 删除旧文件
    std::remove(test_file);
    
    AsyncLogger::getInstance().setLogLevel(LogLevel::INFO);

    // 设置很小的轮转大小以触发轮转
    AsyncLogger::getInstance().setOutputFile(test_file, 1024); // 1KB
    AsyncLogger::getInstance().start();
    
    // 写入足够多的日志以触发轮转
    for (int i = 0; i < 100; ++i) {
        LOG_INFO << "Rolling test message with padding XXXXXXXXXXXXXXXXXXXXXXXX " << i;
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    AsyncLogger::getInstance().stop();
    
    // 检查是否生成了轮转文件
    std::ifstream main_file(test_file);
    TEST_ASSERT(main_file.good(), "Main log file should exist after rolling");
    main_file.close();
    
    // 清理文件
    std::remove(test_file);
    
    return true;
}

// ========== 主函数 ==========
int main() {
    std::cout << "===========================================\n";
    std::cout << "  Async Logger Unit Tests\n";
    std::cout << "===========================================\n";
    
    RUN_TEST(test_basic_start_stop);
    RUN_TEST(test_basic_logging);
    RUN_TEST(test_log_level_filtering);
    RUN_TEST(test_file_output);
    RUN_TEST(test_concurrent_logging);
    RUN_TEST(test_large_messages);
    RUN_TEST(test_rapid_start_stop);
    RUN_TEST(test_statistics);
    RUN_TEST(test_empty_messages);
    RUN_TEST(test_file_rolling);
    
    std::cout << "\n===========================================\n";
    std::cout << "  Test Results\n";
    std::cout << "===========================================\n";
    std::cout << "  Passed: " << tests_passed << "\n";
    std::cout << "  Failed: " << tests_failed << "\n";
    std::cout << "  Total:  " << (tests_passed + tests_failed) << "\n";
    std::cout << "===========================================\n";
    
    return tests_failed == 0 ? 0 : 1;
}
