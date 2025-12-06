/**
 * @file logger.h
 * @brief 工业级异步双缓冲日志系统（性能调优版）
 * 
 * 核心设计：
 * 1. 真正的双缓冲设计（currentBuffer + nextBuffer）
 * 2. 缓冲区预分配和复用，避免频繁内存分配
 * 3. 前端短临界区：锁内仅做拷贝与缓冲区交换
 * 4. 后台线程批量写入，减少系统调用
 * 5. 智能背压控制，关键日志（ERROR/FATAL）优先保证
 * 6. 原子化文件轮转，支持日志归档
 * 7. 性能调优（P1）：栈上定长缓冲 LogStream（零堆分配）+ 秒级时间戳缓存
 * 8. 性能调优（P2）：可配置 fsync 持久化策略 + 队列积压预警
 */

#ifndef TINYMQ_COMMON_LOGGER_H
#define TINYMQ_COMMON_LOGGER_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tinymq {
namespace common {

// ========== 日志级别 ==========
enum class LogLevel {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3,
    FATAL = 4
};

// ========== 前置声明 ==========
class AsyncLogger;

// ========== FixedBuffer：固定大小缓冲区 ==========
template<size_t SIZE>
class FixedBuffer {
public:
    FixedBuffer() : cur_(data_) {}

    void append(const char* buf, size_t len) {
        if (avail() >= len) {
            memcpy(cur_, buf, len);
            cur_ += len;
        }
    }

    const char* data() const { return data_; }
    size_t length() const { return static_cast<size_t>(cur_ - data_); }
    char* current() { return cur_; }
    size_t avail() const { return static_cast<size_t>(end() - cur_); }
    void add(size_t len) { cur_ += len; }
    void reset() { cur_ = data_; }
    void bzero() { memset(data_, 0, sizeof(data_)); }

private:
    const char* end() const { return data_ + sizeof(data_); }

    char data_[SIZE];
    char* cur_;
};

// ========== LogStream：栈上定长缓冲日志构造器（零堆分配） ==========
// 相比 ostringstream 版本：
//   - 零堆分配：ostringstream 构造内部 stringbuf 分配 + str() 拷贝各一次，全部消除
//   - 无 iostream 虚函数调用：operator<< 直接写入栈上定长缓冲，数字格式化手工实现
//   - 时间戳秒级缓存：每秒最多一次 localtime_r/strftime，毫秒手工补零
// 注意：单条日志上限 kMaxStreamSize（16KB），超出部分截断（原 ostringstream 版无长度上限）
class LogStream {
public:
    static constexpr int kMaxStreamSize = 16 * 1024;
    using Buffer = FixedBuffer<kMaxStreamSize>;

    LogStream(LogLevel level, const char* file, int line);
    ~LogStream();

    // 支持流式输出：LOG_INFO << "value=" << 42;
    LogStream& operator<<(bool v)                 { buffer_.append(v ? "1" : "0", 1); return *this; }
    LogStream& operator<<(char v)                 { buffer_.append(&v, 1); return *this; }
    LogStream& operator<<(short v)                { return *this << static_cast<int>(v); }
    LogStream& operator<<(unsigned short v)       { return *this << static_cast<unsigned int>(v); }
    LogStream& operator<<(int v);
    LogStream& operator<<(unsigned int v);
    LogStream& operator<<(long v)                 { return *this << static_cast<long long>(v); }
    LogStream& operator<<(unsigned long v)        { return *this << static_cast<unsigned long long>(v); }
    LogStream& operator<<(long long v);
    LogStream& operator<<(unsigned long long v);
    LogStream& operator<<(float v)                { return *this << static_cast<double>(v); }
    LogStream& operator<<(double v);
    LogStream& operator<<(const char* v);
    LogStream& operator<<(const std::string& v)   { return *this << v.c_str(); }

private:
    LogLevel level_;
    const char* file_;
    int line_;
    Buffer buffer_;

    friend class AsyncLogger;
};

// ========== AsyncLogger：异步双缓冲日志核心 ==========
class AsyncLogger {
public:
    // 缓冲区大小：4MB（可根据实际情况调整）
    static constexpr size_t kLargeBuffer = 4 * 1024 * 1024;
    static constexpr size_t kSmallBuffer = 4 * 1024;
    
    using Buffer = FixedBuffer<kLargeBuffer>;
    using BufferPtr = std::unique_ptr<Buffer>;
    using BufferVector = std::vector<BufferPtr>;

    static AsyncLogger& getInstance();

    // 禁止拷贝和赋值
    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;

    // ========== 配置接口 ==========
    void start();
    void stop();
    
    /**
     * @brief 设置输出文件
     * @param filename 日志文件路径（空字符串表示输出到 stdout）
     * @param rollSize 文件轮转大小（字节），默认 500MB
     */
    void setOutputFile(const std::string& filename, size_t rollSize = 500 * 1024 * 1024);
    
    /**
     * @brief 设置日志级别
     */
    void setLogLevel(LogLevel level) {
        level_.store(level, std::memory_order_release);
    }

    LogLevel getLogLevel() const {
        return level_.load(std::memory_order_acquire);
    }

    /**
     * @brief 设置刷新间隔（秒）
     * @note 后台线程会原子读取该值，运行中调用是安全的
     */
    void setFlushInterval(int seconds) {
        flushInterval_.store(seconds, std::memory_order_relaxed);
    }

    /**
     * @brief P2: 设置 fsync 持久化策略（运行中调用安全）
     * @param enable true  = 每次后台 flush 后 fsync 强制落盘。
     *                       崩溃/断电时最多丢失当前未 flush 缓冲，换取数据安全；降低吞吐。
     *               false = 默认。数据写入内核页缓存后由内核异步回写，
     *                       崩溃/断电可能丢失最近数秒日志，换取最高吞吐。
     */
    void setFsyncOnFlush(bool enable) {
        fsyncOnFlush_.store(enable, std::memory_order_relaxed);
    }

    /**
     * @brief 获取统计信息
     */
    struct Stats {
        uint64_t totalLogs;       // 总日志数
        uint64_t droppedLogs;     // 背压丢弃的缓冲区数量（单位：缓冲区个数，非日志条数）
        uint64_t bytesWritten;    // 已写入字节数
        size_t queuedBuffers;     // 队列中缓冲区数量
    };
    Stats getStats() const;

private:
    AsyncLogger();
    ~AsyncLogger();

    // 前端提交日志（由 LogStream 析构时调用）
    void append(const char* logline, size_t len, LogLevel level);
    friend class LogStream;

    // 后台线程函数
    void bgThreadFunc();

    // 文件操作
    void rollFile();
    void writeToFile(const char* data, size_t len);

    // ========== 前端数据（多生产者访问，需要锁保护）==========
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    
    BufferPtr currentBuffer_;      // 当前写入缓冲区
    BufferPtr nextBuffer_;         // 备用缓冲区（预分配）
    BufferVector buffers_;         // 满缓冲区队列

    // ========== 后台线程数据 ==========
    std::thread bgThread_;
    std::atomic<bool> running_{false};

    // ========== 配置项 ==========
    std::atomic<LogLevel> level_{LogLevel::INFO};
    std::atomic<int> flushInterval_{3};       // 刷新间隔（秒），后台线程原子读取
    std::atomic<bool> fsyncOnFlush_{false};   // P2: fsync 持久化策略开关

    // ========== 文件管理 ==========
    FILE* file_{nullptr};
    std::string filename_;
    size_t rollSize_{500 * 1024 * 1024};  // 默认 500MB
    size_t currentFileWritten_{0};

    // ========== 统计信息 ==========
    std::atomic<uint64_t> totalLogs_{0};
    std::atomic<uint64_t> droppedLogs_{0};
    std::atomic<uint64_t> bytesWritten_{0};
};

// ========== 便捷宏定义 ==========
#define LOG_DEBUG \
    if (tinymq::common::AsyncLogger::getInstance().getLogLevel() <= tinymq::common::LogLevel::DEBUG) \
        tinymq::common::LogStream(tinymq::common::LogLevel::DEBUG, __FILE__, __LINE__)

#define LOG_INFO \
    if (tinymq::common::AsyncLogger::getInstance().getLogLevel() <= tinymq::common::LogLevel::INFO) \
        tinymq::common::LogStream(tinymq::common::LogLevel::INFO, __FILE__, __LINE__)

#define LOG_WARN \
    if (tinymq::common::AsyncLogger::getInstance().getLogLevel() <= tinymq::common::LogLevel::WARN) \
        tinymq::common::LogStream(tinymq::common::LogLevel::WARN, __FILE__, __LINE__)

#define LOG_ERROR \
    tinymq::common::LogStream(tinymq::common::LogLevel::ERROR, __FILE__, __LINE__)

#define LOG_FATAL \
    tinymq::common::LogStream(tinymq::common::LogLevel::FATAL, __FILE__, __LINE__)

} // namespace common
} // namespace tinymq

#endif // TINYMQ_COMMON_LOGGER_H
