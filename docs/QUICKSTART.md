# 快速开始指南

5 分钟上手异步日志系统！

## 1️⃣ 获取代码

```bash
# 克隆或下载项目
git clone https://github.com/your-repo/async-logger.git
cd async-logger
```

或者直接复制这两个文件到你的项目：
- `logger.h`
- `logger.cpp`

---

## 2️⃣ 编译

### Linux

```bash
# 方式1：使用构建脚本（推荐）
chmod +x build.sh
./build.sh

# 方式2：使用 Makefile
make

# 方式3：使用 CMake
mkdir build && cd build
cmake ..
make

# 方式4：手动编译
g++ -std=c++11 -pthread -O2 logger.cpp example.cpp -o logger_test
```

---

## 3️⃣ 运行示例

```bash
# Linux
./logger_test
```

你会看到类似输出：
```
===========================================
  Async Double-Buffer Logger Examples
===========================================

========== Example 1: Basic Usage ==========
2026-08-09 15:32:45.123 [DEBUG] example.cpp:42 - This is a debug message
2026-08-09 15:32:45.124 [INFO ] example.cpp:43 - Server started on port 8080
...
```

---

## 4️⃣ 第一个程序

创建 `my_app.cpp`：

```cpp
#include "logger.h"
#include <thread>
#include <chrono>

using namespace tinymq::common;

int main() {
    // 1. 启动日志系统
    AsyncLogger::getInstance().start();
    
    // 2. 配置（可选）
    AsyncLogger::getInstance().setLogLevel(LogLevel::INFO);
    AsyncLogger::getInstance().setOutputFile("my_app.log", 100 * 1024 * 1024);
    
    // 3. 记录日志
    LOG_INFO << "Application started";
    LOG_WARN << "Memory usage: " << 85.5 << "%";
    LOG_ERROR << "Connection failed";
    
    // 4. 等待日志写入
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 5. 优雅关闭
    AsyncLogger::getInstance().stop();
    
    LOG_INFO << "Application stopped";
    
    return 0;
}
```

编译运行：
```bash
# Linux
g++ -std=c++11 -pthread logger.cpp my_app.cpp -o my_app
./my_app
```

---

## 5️⃣ 核心 API

### 启动和停止
```cpp
AsyncLogger::getInstance().start();  // 启动后台线程
AsyncLogger::getInstance().stop();   // 停止并刷盘
```

### 配置
```cpp
// 设置日志级别
AsyncLogger::getInstance().setLogLevel(LogLevel::INFO);

// 设置输出文件（第二个参数是轮转大小，单位字节）
AsyncLogger::getInstance().setOutputFile("app.log", 100 * 1024 * 1024);

// 设置刷新间隔（秒）
AsyncLogger::getInstance().setFlushInterval(3);
```

### 记录日志
```cpp
LOG_DEBUG << "Debug message";
LOG_INFO << "Info message with value: " << 42;
LOG_WARN << "Warning: disk space low";
LOG_ERROR << "Error: " << error_msg;
LOG_FATAL << "Fatal error, exiting";
```

### 获取统计
```cpp
auto stats = AsyncLogger::getInstance().getStats();
std::cout << "Total logs: " << stats.totalLogs << "\n";
std::cout << "Dropped: " << stats.droppedLogs << "\n";
std::cout << "Bytes written: " << stats.bytesWritten << "\n";
```

---

## 6️⃣ 常见场景

### 场景1：输出到控制台
```cpp
AsyncLogger::getInstance().start();
// 不调用 setOutputFile()，默认输出到 stdout
LOG_INFO << "This goes to console";
```

### 场景2：多线程应用
```cpp
void worker_thread(int id) {
    LOG_INFO << "Thread " << id << " started";
    // 业务逻辑...
    LOG_INFO << "Thread " << id << " finished";
}

int main() {
    AsyncLogger::getInstance().start();
    AsyncLogger::getInstance().setOutputFile("app.log");
    
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back(worker_thread, i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    AsyncLogger::getInstance().stop();
}
```

### 场景3：长期运行服务
```cpp
#include <signal.h>

void signal_handler(int sig) {
    LOG_INFO << "Received signal " << sig << ", shutting down...";
    AsyncLogger::getInstance().stop();
    exit(0);
}

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    AsyncLogger::getInstance().start();
    AsyncLogger::getInstance().setOutputFile("server.log", 500 * 1024 * 1024);
    AsyncLogger::getInstance().setLogLevel(LogLevel::INFO);
    
    LOG_INFO << "Server started";
    
    // 主循环
    while (true) {
        // 处理请求...
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    return 0;
}
```

---

## 7️⃣ 性能测试

运行基准测试（推荐使用构建脚本）：
```bash
./build.sh bench          # 跑完全部 6 组（编译 + 运行），约 30 秒
./logger_benchmark 6      # 只跑第 6 组：纯前端吞吐（输出到 /dev/null）
```

或手动编译运行（先 `mkdir -p output`）：
```bash
g++ -std=c++11 -pthread -O2 logger.cpp tests/benchmark.cpp -o output/logger_benchmark
cd output && ./logger_benchmark 1  # 单线程吞吐量
./logger_benchmark 2  # 多线程性能
./logger_benchmark 3  # 不同消息长度
./logger_benchmark 4  # 压力测试
./logger_benchmark 5  # 级别过滤
./logger_benchmark 6  # 纯前端（不落盘）
```

实测示例（VM：Ubuntu 22.04 / GCC 11.4 / -O2，完整数据见 `docs/PERFORMANCE_TUNING.md`）：
```
========== Benchmark 2: Multi-threaded Performance ==========
Results:
  Threads: 10
  Total logs: 500000
  Total time: 1069.90 ms
  Throughput: 467335.14 logs/sec
  Bandwidth: 39.35 MB/sec

Latency Distribution:
  Average: 20.69 μs
  P50: 1.60 μs
  P95: 10.63 μs
  P99: 412.08 μs
```

> 不同机器/环境数字有波动（纯 CPU 场景可达 ±40%），请以本机实测为准。

---

## 8️⃣ 单元测试

运行单元测试：
```bash
# 编译测试
g++ -std=c++11 -pthread logger.cpp unit_test.cpp -o test

# 运行测试
./test
```

输出：
```
===========================================
  Async Logger Unit Tests
===========================================

Running test_basic_start_stop...
✓ test_basic_start_stop passed

Running test_basic_logging...
✓ test_basic_logging passed

...

===========================================
  Test Results
===========================================
  Passed: 10
  Failed: 0
  Total:  10
===========================================
```

---

## 9️⃣ 集成到现有项目

### CMake 项目
```cmake
# 添加到 CMakeLists.txt
add_library(asynclogger STATIC logger.cpp)
target_include_directories(asynclogger PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asynclogger PUBLIC Threads::Threads)

# 链接到你的目标
target_link_libraries(your_app asynclogger)
```

### Makefile 项目
```makefile
# 添加到 Makefile
LDFLAGS += -pthread
OBJS += logger.o

logger.o: logger.cpp logger.h
	$(CXX) $(CXXFLAGS) -c logger.cpp

your_app: your_app.o logger.o
	$(CXX) $^ $(LDFLAGS) -o $@
```

### 手动集成
```bash
# 1. 复制文件
cp logger.h logger.cpp your_project/src/

# 2. 编译时包含
g++ -std=c++11 -pthread your_app.cpp logger.cpp -o your_app
```

---

## 🔟 下一步

- 📖 阅读 [README.md](README.md) 了解详细功能
- 🏗️ 阅读 [DESIGN.md](DESIGN.md) 了解架构设计
- ❓ 阅读 [FAQ.md](FAQ.md) 解决常见问题
- 📝 查看 [CHANGELOG.md](CHANGELOG.md) 了解更新历史

---

## 💡 快速技巧

### 调试模式
```cpp
// 开发时启用详细日志
#ifdef DEBUG
    AsyncLogger::getInstance().setLogLevel(LogLevel::DEBUG);
#else
    AsyncLogger::getInstance().setLogLevel(LogLevel::INFO);
#endif
```

### 条件日志
```cpp
// 避免不必要的计算
if (AsyncLogger::getInstance().getLogLevel() <= LogLevel::DEBUG) {
    std::string expensive_data = computeExpensiveData();
    LOG_DEBUG << "Data: " << expensive_data;
}
```

### 结构化信息
```cpp
LOG_INFO << "Request"
         << " | method=" << method
         << " | path=" << path
         << " | status=" << status
         << " | latency=" << latency_ms << "ms";
```

---

## ⚠️ 注意事项

1. **必须调用 `start()`**
   ```cpp
   AsyncLogger::getInstance().start();  // ← 别忘了这一行！
   ```

2. **程序退出前调用 `stop()`**
   ```cpp
   AsyncLogger::getInstance().stop();  // ← 确保日志落盘
   ```

3. **不要在信号处理器中记录日志**
   ```cpp
   // ❌ 不安全（可能死锁）
   void signal_handler(int sig) {
       LOG_INFO << "Signal received";  // 危险！
   }
   
   // ✅ 安全
   void signal_handler(int sig) {
       AsyncLogger::getInstance().stop();
       exit(0);
   }
   ```

4. **检查磁盘空间**
   ```bash
   # 定期清理旧日志
   find /var/log -name "*.log.*" -mtime +7 -delete
   ```

---

## 🆘 遇到问题？

1. 查看 [FAQ.md](FAQ.md) 常见问题
2. 运行 `unit_test` 检查基本功能
3. 启用 DEBUG 级别日志排查
4. 检查文件权限和磁盘空间
5. 提交 Issue 或发送邮件

---

**恭喜！你已经掌握了异步日志系统的基本使用 🎉**

现在开始在你的项目中享受高性能日志吧！
