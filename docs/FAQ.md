# 常见问题 FAQ

## 基础问题

### Q1: 为什么需要异步日志？

**A:** 同步日志的问题：

- 每次日志都要写磁盘（慢）
- 多线程竞争文件锁（竞争严重）
- 阻塞业务线程（影响性能）

异步日志的优势：

- 前端快速写入内存缓冲区（微秒级）
- 后台批量写入磁盘（减少系统调用）
- 业务线程无阻塞（高吞吐）

**性能对比**：

```
同步日志：~1,000 条/秒（多线程场景）
异步日志：~1,000,000 条/秒（100x 提升）
```

***

### Q2: 什么是双缓冲？

**A:** 双缓冲是一种经典的生产者 - 消费者模式：

```
前端（生产者）        后台（消费者）
   ↓                      ↓
currentBuffer  ←→   批量写入磁盘
nextBuffer     ←→   缓冲区复用
```

**工作流程**：

1. 前端写入 currentBuffer（快）
2. currentBuffer 满时切换到 nextBuffer
3. 满缓冲区交给后台线程
4. 后台批量写入磁盘
5. 空缓冲区归还给前端复用

***

### Q3: 会丢失日志吗？

**A:** 正常情况下不会丢失，但以下情况可能丢失：

1. **程序崩溃（段错误、断言失败）**
   - 缓冲区中的日志未刷盘
   - 解决：定期刷盘（默认 3 秒）
2. **突然断电**
   - 操作系统缓存未落盘
   - 解决：使用 `fsync` 或 `O_DIRECT`
3. **背压过载**
   - 日志速度 > 磁盘速度
   - 解决：自动丢弃中间缓冲区

**建议**：

- 关键日志（ERROR/FATAL）单独处理
- 优雅关闭时调用 `stop()` 刷盘
- 重要日志考虑同步写入或双写

***

### Q4: 如何优雅关闭？

**A:** 确保在程序退出前调用 `stop()`：

```cpp
#include <signal.h>

void signal_handler(int sig) {
    AsyncLogger::getInstance().stop();
    exit(0);
}

int main() {
    signal(SIGINT, signal_handler);   // Ctrl+C
    signal(SIGTERM, signal_handler);  // kill

    AsyncLogger::getInstance().start();

    // 业务代码...

    AsyncLogger::getInstance().stop();
    return 0;
}
```

***

## 使用问题

### Q5: 可以在多个线程中使用吗？

**A:** 完全可以，这正是设计目标：

```cpp
void thread_func(int id) {
    LOG_INFO << "Thread " << id << " is running";
}

int main() {
    AsyncLogger::getInstance().start();

    std::vector<std::thread> threads;
    for (int i = 0; i < 100; ++i) {
        threads.emplace_back(thread_func, i);
    }

    for (auto& t : threads) {
        t.join();
    }

    AsyncLogger::getInstance().stop();
}
```

**线程安全保证**：

- 前端多线程并发写入（互斥锁保护）
- 后台单线程顺序写入（锁外）
- 无数据竞争

***

### Q6: 如何输出到控制台？

**A:** 默认输出到 stdout：

```cpp
// 方式1：不设置文件（默认 stdout）
AsyncLogger::getInstance().start();

// 方式2：显式设置为空字符串
AsyncLogger::getInstance().setOutputFile("", 0);
```

***

### Q7: 如何同时输出到文件和控制台？

**A:** 当前实现只支持单一输出。如需多输出：

**方案 1：使用 `tee` 命令**

```
./your_app | tee app.log
```

**方案 2：扩展代码支持多 Sink**

```cpp
// 需要修改代码添加 Sink 机制
logger.addSink(new FileSink("app.log"));
logger.addSink(new ConsoleSink(stdout));
```

***

### Q8: 日志格式可以自定义吗？

**A:** 当前格式固定：

```
2026-08-09 15:32:45.123 [INFO] main.cpp:42 - Your message
```

**自定义方式**：

1. 修改 `LogStream::LogStream()` 构造函数
2. 调整时间格式、级别显示等

```cpp
// 示例：修改为 JSON 格式
stream_ << "{"
        << "\"time\":\"" << time_str << "\","
        << "\"level\":\"" << level_str << "\","
        << "\"file\":\"" << basename(file_) << "\","
        << "\"line\":" << line_ << ","
        << "\"msg\":\"";

// 在析构时添加结束
stream_ << "\"}";
```

***

### Q9: 如何记录二进制数据？

**A:** 转换为十六进制字符串：

```cpp
void log_hex(const void* data, size_t len) {
    std::ostringstream oss;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < len; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(bytes[i]) << " ";
    }
    LOG_INFO << "Binary data: " << oss.str();
}

// 使用
char data[] = {0x01, 0x02, 0x03, 0xAB, 0xCD};
log_hex(data, sizeof(data));

// 输出: Binary data: 01 02 03 ab cd
```

***

## 性能问题

### Q10: 性能瓶颈在哪里？

**A:** 主要瓶颈：

1. **磁盘 I/O**（最主要）
   - 机械硬盘：~100 MB/s
   - SSD：~500 MB/s
   - NVMe：~2000 MB/s
2. **锁竞争**（次要）
   - 缓冲区切换时加锁
   - 优化：减少切换频率
3. **内存分配**（已优化）
   - 预分配 + 复用
   - 几乎无动态分配

**优化建议**：

```
// 1. 使用 SSD 存储日志
// 2. 增大缓冲区大小（减少切换）
// 3. 降低日志级别（过滤无用日志）
// 4. 增大文件轮转阈值（减少文件操作）
```

***

### Q11: 前端写入会阻塞吗？

**A:** 每次 append 都会获取全局互斥锁，但临界区极短 —— 常见情况只在锁内做一次 memcpy，缓冲区切换时也仅做指针交换（不涉及 I/O），配合 4MB 大缓冲，加锁频率被摊薄到每 4MB 一次：

```
// 真实实现（AsyncLogger::append）：
// 1. 级别过滤（原子读，无锁）
// 2. 获取互斥锁，临界区内仅 memcpy 或指针交换
// 3. 缓冲区写满时 notify 后台线程后解锁
// 4. 磁盘 I/O 完全在后台线程、锁外进行
```

> 注：当前实现并非 "无锁快速路径"；真正的前端无锁化（thread_local 分片缓冲 / 无锁队列）是后续优化方向。1.1.0 已完成前端零分配格式化（自研 LogStream），无锁仍为下一步。

**延迟实测数据（10 线程，VM：Ubuntu 22.04 / GCC 11.4 / -O2）**：

```
P50 延迟: 1.6 ~ 2.0 μs
P90 延迟: 5.7 ~ 7.5 μs
P95 延迟: 10.6 ~ 12.6 μs
P99 延迟: 410 ~ 430 μs
P999 延迟: 3.2 ~ 4.4 ms
```

> 完整实测与口径见 `docs/PERFORMANCE_TUNING.md`；不同环境有波动，请以本机 `./build.sh bench` 实测为准。

***

### Q12: 如何提升性能？

**A:** 多方面优化：

**1. 硬件层面**

```
# 使用更快的磁盘
SSD > 机械硬盘
NVMe > SATA SSD

# 增加内存
更大缓冲区 = 更少切换
```

**2. 软件层面**

```cpp
// 过滤低级别日志
logger.setLogLevel(LogLevel::INFO);

// 增大刷新间隔
logger.setFlushInterval(5);  // 5 秒

// 避免复杂计算
// ❌ LOG_INFO << "Hash: " << expensiveCompute();
// ✅ 先判断级别再计算
```

**3. 系统层面**

```
# 调整文件系统参数
mount -o noatime,nodiratime /dev/sda1 /logs

# 使用 tmpfs（内存文件系统）
mount -t tmpfs -o size=1G tmpfs /tmp/logs
```

***

### Q13: 内存占用多少？

**A:** 正常情况：16MB，极端情况：~100MB

**详细分析**：

```
固定内存（16MB）：
  currentBuffer:  4 MB
  nextBuffer:     4 MB
  newBuffer1:     4 MB
  newBuffer2:     4 MB

动态内存（0-100MB）：
  buffers_ 队列:  0-25 个缓冲区（0-100MB）

其他：
  线程栈:         ~2 MB
  其他开销:       < 1 MB
```

**背压控制**：

- 队列超过 25 个缓冲区时丢弃中间部分
- 最大内存：16 + 100 = 116 MB

***

## 配置问题

### Q14: 文件轮转大小如何设置？

**A:** 根据场景选择：

```cpp
// 低流量服务（每天几百 MB）
logger.setOutputFile("app.log", 500 * 1024 * 1024);  // 500MB

// 中等流量服务（每天几 GB）
logger.setOutputFile("app.log", 100 * 1024 * 1024);  // 100MB

// 高流量服务（每天几十 GB）
logger.setOutputFile("app.log", 50 * 1024 * 1024);   // 50MB

// 超高流量服务（每小时 GB 级别）
logger.setOutputFile("app.log", 10 * 1024 * 1024);   // 10MB
```

**考虑因素**：

- 磁盘空间
- 日志分析工具限制
- 归档周期

***

### Q15: 刷新间隔如何设置？

**A:** 权衡实时性和性能：

```cpp
// 实时性优先（金融、交易）
logger.setFlushInterval(1);  // 1 秒

// 平衡（默认推荐）
logger.setFlushInterval(3);  // 3 秒

// 性能优先（大数据、批处理）
logger.setFlushInterval(10); // 10 秒
```

**注意**：

- 间隔越短，实时性越好，但性能下降
- 间隔越长，吞吐量越高，但崩溃时丢失更多

***

## 故障排查

### Q16: 日志文件为空？

**A:** 可能原因：

1. **未调用 `start()`**

```cpp
// ❌ 忘记启动
LOG_INFO << "message";

// ✅ 正确
AsyncLogger::getInstance().start();
LOG_INFO << "message";
AsyncLogger::getInstance().stop();
```

2. **未等待刷盘**

```cpp
// ❌ 立即退出
LOG_INFO << "message";
return 0;  // 日志还在缓冲区

// ✅ 等待刷盘
LOG_INFO << "message";
std::this_thread::sleep_for(std::chrono::seconds(1));
AsyncLogger::getInstance().stop();
```

3. **文件权限问题**

```
# 检查目录权限
ls -ld /path/to/log/dir

# 检查文件权限
ls -l app.log
```

***

### Q17: 日志乱序？

**A:** 这是正常现象：

**原因**：

- 多线程并发写入
- 操作系统调度不确定

**示例**：

```
Thread 1: [15:30:01.100] Message A
Thread 2: [15:30:01.099] Message B  ← 时间戳更早，但后写入
```

**解决方案**：

```
# 按时间戳排序
sort app.log

# 或使用日志分析工具
cat app.log | awk '{print $1, $2, $0}' | sort
```

***

### Q18: 日志丢失？

**A:** 检查统计信息：

```cpp
auto stats = AsyncLogger::getInstance().getStats();
std::cout << "Total logs: " << stats.totalLogs << "\n";
std::cout << "Dropped logs: " << stats.droppedLogs << "\n";
```

**可能原因**：

1. **级别过滤**

```cpp
logger.setLogLevel(LogLevel::WARN);
LOG_INFO << "Not logged";  // INFO < WARN，被过滤
```

2. **背压丢弃**

```
日志速度: 1,000,000 条/秒
磁盘速度: 100,000 条/秒
结果: 90% 被丢弃
```

3. **程序崩溃**

```
缓冲区日志未刷盘 → 丢失
```

**解决**：

- 降低日志产生速度
- 提高磁盘性能
- 优雅关闭程序

***

### Q19: 编译错误？

**A:** 常见问题：

**1. C++ 版本不足**

```
# 需要 C++11 或更高
g++ -std=c++11 logger.cpp example.cpp
```

**2. 缺少线程库**

```
# Linux 需要链接 pthread
g++ -std=c++11 -pthread logger.cpp example.cpp
```

***

### Q20: 如何调试？

**A:** 多种方法：

**1. 启用调试日志**

```cpp
AsyncLogger::getInstance().setLogLevel(LogLevel::DEBUG);
```

**2. 查看统计信息**

```cpp
auto stats = AsyncLogger::getInstance().getStats();
std::cout << "Total: " << stats.totalLogs << "\n"
          << "Dropped: " << stats.droppedLogs << "\n"
          << "Bytes: " << stats.bytesWritten << "\n"
          << "Queued: " << stats.queuedBuffers << "\n";
```

**3. 使用 gdb/lldb**

```
gdb ./your_app
(gdb) break AsyncLogger::append
(gdb) run
(gdb) print *currentBuffer_
```

**4. 使用 valgrind**

```
valgrind --leak-check=full ./your_app
```

**5. 使用 strace/ltrace**

```
strace -e trace=write,open,close ./your_app
```

***

## 高级话题

### Q21: 如何集成到现有项目？

**A:** 三步集成：

**1. 复制文件**

```
cp logger.h logger.cpp your_project/
```

**2. 修改构建系统**

```
# CMakeLists.txt
add_library(logger logger.cpp)
target_link_libraries(your_app logger pthread)
```

**3. 初始化**

```cpp
#include "logger.h"

int main() {
    AsyncLogger::getInstance().start();
    AsyncLogger::getInstance().setOutputFile("app.log");

    // 你的代码...

    AsyncLogger::getInstance().stop();
}
```

***

### Q22: 可以用于生产环境吗？

**A:** 可以，但需要：

**1. 充分测试**

- 压力测试
- 长时间运行测试
- 故障注入测试

**2. 监控**

```cpp
// 定期检查统计信息
setInterval([] {
    auto stats = AsyncLogger::getInstance().getStats();
    if (stats.droppedLogs > threshold) {
        alert("High log drop rate!");
    }
}, 60s);
```

**3. 告警**

- 日志丢弃率告警
- 磁盘空间告警
- 文件轮转失败告警

**4. 备份**

- 定期归档日志
- 远程备份

***

### Q23: 如何扩展功能？

**A:** 开放扩展：

**1. 添加新的日志级别**

```cpp
enum class LogLevel {
    TRACE = -1,  // 新增
    DEBUG = 0,
    INFO  = 1,
    // ...
};
```

**2. 添加多 Sink 支持**

```cpp
class Sink {
public:
    virtual void write(const char* data, size_t len) = 0;
};

class AsyncLogger {
    std::vector<std::unique_ptr<Sink>> sinks_;
};
```

**3. 添加日志过滤器**

```cpp
class Filter {
public:
    virtual bool shouldLog(LogLevel level, const char* msg) = 0;
};
```

***

### Q24: 性能对比数据？

**A:** 本机实测数据（VM：Ubuntu 22.04 / GCC 11.4 / -O2，详见 `docs/PERFORMANCE_TUNING.md`）：

**单线程吞吐量**：

```
Async logger: 20~40 万 logs/sec（优化前 1.19 万，提升约 20~30 倍）
```

**多线程吞吐量（10 线程）**：

```
Async logger: 38~47 万 logs/sec（优化前 2.55 万）
```

**50 线程压力（100 万条）**：

```
Async logger: 57~60 万 logs/sec，零丢弃
```

**延迟分布（10 线程）**：

```
P50:  1.6 ~ 2.0 μs
P95:  10.6 ~ 12.6 μs
P99:  410 ~ 430 μs
```

> 说明：原文档中的 Sync logger 对比数字为示例估算（未做同步版对比实测），已移除。数字在不同环境有波动，请以本机 `./build.sh bench` 实测为准。

***

### Q25: 未来计划？

**A:** 可能的改进方向：

1. **无锁队列**：替代 mutex
2. **多 Sink**：同时输出到多个目标
3. **结构化日志**：JSON 格式
4. **压缩归档**：自动 gzip
5. **网络传输**：发送到日志服务器
6. **性能分析**：内置 profiling
7. **日志采样**：高频事件采样
8. **动态配置**：运行时调整级别

***

## 联系方式

如有其他问题，请通过以下方式联系：


- Email: 2723037673@qq.com
- 文档：参考 README.md 和 DESIGN.md
