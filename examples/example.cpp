/**
 * @file example.cpp
 * @brief 异步双缓冲日志系统使用示例
 * 
 * 编译命令：
 *   g++ -std=c++11 -pthread logger.cpp example.cpp -o logger_test
 *   或
 *   cl /EHsc /std:c++17 logger.cpp example.cpp /Fe:logger_test.exe
 */

#include "logger.h"
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <random>

using namespace tinymq::common;

// ========== 示例1：基本使用 ==========
void example_basic() {
    std::cout << "\n========== Example 1: Basic Usage ==========\n";
    
    // 启动异步日志系统
    AsyncLogger::getInstance().start();
    AsyncLogger::getInstance().setLogLevel(LogLevel::DEBUG);
    
    // 使用流式输出
    LOG_DEBUG << "This is a debug message";
    LOG_INFO << "Server started on port " << 8080;
    LOG_WARN << "Memory usage: " << 85.5 << "%";
    LOG_ERROR << "Connection failed: " << "timeout";
    
    // 等待日志写入完成
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 停止日志系统
    AsyncLogger::getInstance().stop();
}

// ========== 示例2：多线程并发写入 ==========
void worker_thread(int threadId, int numLogs) {
    for (int i = 0; i < numLogs; ++i) {
        LOG_INFO << "Thread-" << threadId << " log #" << i 
                 << " with some additional data: " << i * 3.14159;
        
        // 模拟业务逻辑
        if (i % 100 == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
}

void example_multithreaded() {
    std::cout << "\n========== Example 2: Multi-threaded Logging ==========\n";
    
    AsyncLogger::getInstance().start();
    AsyncLogger::getInstance().setLogLevel(LogLevel::INFO);
    
    const int numThreads = 10;
    const int logsPerThread = 1000;
    
    auto startTime = std::chrono::steady_clock::now();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back(worker_thread, i, logsPerThread);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    // 等待所有日志写入完成
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 获取统计信息
    auto stats = AsyncLogger::getInstance().getStats();
    
    std::cout << "\nPerformance Report:\n";
    std::cout << "  Total logs: " << stats.totalLogs << "\n";
    std::cout << "  Dropped logs: " << stats.droppedLogs << "\n";
    std::cout << "  Bytes written: " << stats.bytesWritten << " bytes\n";
    std::cout << "  Time elapsed: " << duration.count() << " ms\n";
    std::cout << "  Throughput: " << (stats.totalLogs * 1000.0 / duration.count()) 
              << " logs/sec\n";
    
    AsyncLogger::getInstance().stop();
}

// ========== 示例3：文件输出与轮转 ==========
void example_file_output() {
    std::cout << "\n========== Example 3: File Output & Rolling ==========\n";
    
    // 设置输出到文件，文件大小超过 1MB 时轮转（须在 start() 之前配置）
    AsyncLogger::getInstance().setOutputFile("test.log", 1 * 1024 * 1024);
    AsyncLogger::getInstance().setLogLevel(LogLevel::DEBUG);
    AsyncLogger::getInstance().start();
    
    // 写入大量日志以触发文件轮转
    for (int i = 0; i < 5000; ++i) {
        LOG_INFO << "File logging test message #" << i 
                 << " - Some padding text to increase log size: "
                 << "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        
        if (i % 500 == 0) {
            LOG_WARN << "Checkpoint: " << i << " logs written";
        }
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    auto stats = AsyncLogger::getInstance().getStats();
    std::cout << "\nFile Output Stats:\n";
    std::cout << "  Total logs: " << stats.totalLogs << "\n";
    std::cout << "  Bytes written: " << stats.bytesWritten << " bytes ("
              << (stats.bytesWritten / 1024.0 / 1024.0) << " MB)\n";
    
    AsyncLogger::getInstance().stop();
    
    std::cout << "\nCheck 'test.log' and 'test.log.*' files in current directory\n";
}

// ========== 示例4：压力测试 ==========
void stress_test_thread(int threadId, int numLogs) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> levelDist(0, 4);
    
    for (int i = 0; i < numLogs; ++i) {
        int level = levelDist(gen);
        
        switch (level) {
            case 0:
                LOG_DEBUG << "T" << threadId << " DEBUG #" << i;
                break;
            case 1:
                LOG_INFO << "T" << threadId << " INFO #" << i;
                break;
            case 2:
                LOG_WARN << "T" << threadId << " WARN #" << i;
                break;
            case 3:
                LOG_ERROR << "T" << threadId << " ERROR #" << i;
                break;
            case 4:
                LOG_FATAL << "T" << threadId << " FATAL #" << i;
                break;
        }
    }
}

void example_stress_test() {
    std::cout << "\n========== Example 4: Stress Test ==========\n";
    
    AsyncLogger::getInstance().setLogLevel(LogLevel::DEBUG);
    AsyncLogger::getInstance().setOutputFile("stress.log", 100 * 1024 * 1024);
    AsyncLogger::getInstance().start();
    
    const int numThreads = 20;
    const int logsPerThread = 10000;
    
    std::cout << "Starting stress test: " << numThreads << " threads, "
              << logsPerThread << " logs each...\n";
    
    auto startTime = std::chrono::steady_clock::now();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back(stress_test_thread, i, logsPerThread);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // 等待所有日志写入
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    
    auto stats = AsyncLogger::getInstance().getStats();
    
    std::cout << "\nStress Test Results:\n";
    std::cout << "  Total logs: " << stats.totalLogs << "\n";
    std::cout << "  Dropped logs: " << stats.droppedLogs 
              << " (" << (stats.droppedLogs * 100.0 / stats.totalLogs) << "%)\n";
    std::cout << "  Bytes written: " << (stats.bytesWritten / 1024.0 / 1024.0) << " MB\n";
    std::cout << "  Time elapsed: " << duration.count() << " ms\n";
    std::cout << "  Throughput: " << (stats.totalLogs * 1000.0 / duration.count()) 
              << " logs/sec\n";
    std::cout << "  Bandwidth: " << (stats.bytesWritten / 1024.0 / 1024.0 / duration.count() * 1000) 
              << " MB/sec\n";
    
    AsyncLogger::getInstance().stop();
}

// ========== 示例5：级别过滤 ==========
void example_level_filtering() {
    std::cout << "\n========== Example 5: Log Level Filtering ==========\n";
    
    AsyncLogger::getInstance().start();
    
    // 设置为 WARN 级别，DEBUG 和 INFO 将被过滤
    AsyncLogger::getInstance().setLogLevel(LogLevel::WARN);
    
    LOG_DEBUG << "This DEBUG message will NOT be logged";
    LOG_INFO << "This INFO message will NOT be logged";
    LOG_WARN << "This WARN message WILL be logged";
    LOG_ERROR << "This ERROR message WILL be logged";
    LOG_FATAL << "This FATAL message WILL be logged";
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    auto stats = AsyncLogger::getInstance().getStats();
    std::cout << "\nOnly 3 logs should be written (WARN, ERROR, FATAL)\n";
    std::cout << "Total logs: " << stats.totalLogs << "\n";
    
    AsyncLogger::getInstance().stop();
}

// ========== 主函数 ==========
int main(int argc, char* argv[]) {
    std::cout << "===========================================\n";
    std::cout << "  Async Double-Buffer Logger Examples\n";
    std::cout << "===========================================\n";
    
    if (argc > 1) {
        int exampleNum = std::atoi(argv[1]);
        switch (exampleNum) {
            case 1: example_basic(); break;
            case 2: example_multithreaded(); break;
            case 3: example_file_output(); break;
            case 4: example_stress_test(); break;
            case 5: example_level_filtering(); break;
            default:
                std::cout << "Usage: " << argv[0] << " [1-5]\n";
                std::cout << "  1: Basic usage\n";
                std::cout << "  2: Multi-threaded logging\n";
                std::cout << "  3: File output & rolling\n";
                std::cout << "  4: Stress test\n";
                std::cout << "  5: Log level filtering\n";
                return 1;
        }
    } else {
        // 运行所有示例
        example_basic();
        example_multithreaded();
        example_file_output();
        example_stress_test();
        example_level_filtering();
    }
    
    std::cout << "\n===========================================\n";
    std::cout << "  All examples completed!\n";
    std::cout << "===========================================\n";
    
    return 0;
}
