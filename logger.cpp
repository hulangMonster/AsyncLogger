/**
 * @file logger.cpp
 * @brief 工业级异步双缓冲日志系统实现（性能调优版）
 */

#include "logger.h"
#include <cassert>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>

namespace tinymq {
namespace common {

// ========== 辅助函数 ==========
static const char* levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default:              return "UNKNW";
    }
}

static const char* basename(const char* filepath) {
    const char* slash = strrchr(filepath, '/');
    if (!slash) {
        slash = strrchr(filepath, '\\');
    }
    return slash ? slash + 1 : filepath;
}

// ========== P1: 秒级时间戳缓存 ==========
// 原实现每条日志调用 localtime_r + std::put_time（约 20-30μs），
// 是本项目单条日志 83.7μs 开销中最大的一块。
// 优化：每线程 thread_local 缓存秒级时间字符串，跨秒才重新格式化；
//       毫秒由时间戳取模后手工补零，全程零堆分配、零虚函数调用。
namespace {
struct TimestampCache {
    char buf[32];            // "2026-09-16 12:00:00."
    int64_t second = -1;     // 已缓存的秒（-1 表示未初始化）
};
thread_local TimestampCache t_timeCache;
}

// ========== LogStream 实现（P1：零堆分配定长缓冲） ==========
LogStream::LogStream(LogLevel level, const char* file, int line)
    : level_(level), file_(file), line_(line) {

    // 一次时钟调用同时取秒和毫秒
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t sec = nowMs / 1000;
    int ms = static_cast<int>(nowMs % 1000);

    // 跨秒才格式化日期时间（每秒最多一次 localtime_r + strftime）
    if (sec != t_timeCache.second) {
        time_t t = static_cast<time_t>(sec);
        struct tm tm_time;
        localtime_r(&t, &tm_time);
        strftime(t_timeCache.buf, sizeof(t_timeCache.buf), "%Y-%m-%d %H:%M:%S.", &tm_time);
        t_timeCache.second = sec;
    }
    buffer_.append(t_timeCache.buf, strlen(t_timeCache.buf));

    // 毫秒手工补零（替代 setfill/setw 操纵符，避免 iostream 虚调用）
    char msBuf[3] = {
        static_cast<char>('0' + ms / 100),
        static_cast<char>('0' + (ms / 10) % 10),
        static_cast<char>('0' + ms % 10)
    };
    buffer_.append(msBuf, 3);

    buffer_.append(" [", 2);
    buffer_.append(levelToString(level_), strlen(levelToString(level_)));
    buffer_.append("] ", 2);

    const char* base = basename(file_);
    buffer_.append(base, strlen(base));
    buffer_.append(":", 1);

    // 行号：手工十进制转换（替代 iostream 流插入）
    char lineBuf[16];
    char* p = lineBuf + sizeof(lineBuf);
    int ln = line_;
    do { *--p = static_cast<char>('0' + ln % 10); ln /= 10; } while (ln);
    buffer_.append(p, static_cast<size_t>(lineBuf + sizeof(lineBuf) - p));

    buffer_.append(" - ", 3);
}

LogStream::~LogStream() {
    buffer_.append("\n", 1);
    // 原实现 stream_.str() 会产生一次堆分配 + 全量拷贝；
    // 现在直接提交定长缓冲，无中间 std::string，全程零堆分配。
    AsyncLogger::getInstance().append(buffer_.data(), buffer_.length(), level_);
}

// ---------- 整数转字符串：手工倒序填充，避开 iostream 虚函数与 locale 分支 ----------
LogStream& LogStream::operator<<(int v) {
    if (buffer_.avail() < 16) return *this;
    char tmp[16];
    char* p = tmp + sizeof(tmp);
    unsigned int u;
    if (v < 0) { u = static_cast<unsigned int>(-(v + 1)) + 1; *--p = '-'; }
    else       { u = static_cast<unsigned int>(v); }
    do { *--p = static_cast<char>('0' + u % 10); u /= 10; } while (u != 0);
    buffer_.append(p, static_cast<size_t>(tmp + sizeof(tmp) - p));
    return *this;
}

LogStream& LogStream::operator<<(unsigned int v) {
    if (buffer_.avail() < 16) return *this;
    char tmp[16];
    char* p = tmp + sizeof(tmp);
    do { *--p = static_cast<char>('0' + v % 10); v /= 10; } while (v != 0);
    buffer_.append(p, static_cast<size_t>(tmp + sizeof(tmp) - p));
    return *this;
}

LogStream& LogStream::operator<<(long long v) {
    if (buffer_.avail() < 24) return *this;
    char tmp[24];
    char* p = tmp + sizeof(tmp);
    unsigned long long u;
    if (v < 0) { u = static_cast<unsigned long long>(-(v + 1)) + 1; *--p = '-'; }
    else       { u = static_cast<unsigned long long>(v); }
    do { *--p = static_cast<char>('0' + u % 10); u /= 10; } while (u != 0);
    buffer_.append(p, static_cast<size_t>(tmp + sizeof(tmp) - p));
    return *this;
}

LogStream& LogStream::operator<<(unsigned long long v) {
    if (buffer_.avail() < 24) return *this;
    char tmp[24];
    char* p = tmp + sizeof(tmp);
    do { *--p = static_cast<char>('0' + v % 10); v /= 10; } while (v != 0);
    buffer_.append(p, static_cast<size_t>(tmp + sizeof(tmp) - p));
    return *this;
}

LogStream& LogStream::operator<<(double v) {
    // 默认 %.6g（6 位有效数字），与 iostream 默认精度一致
    if (buffer_.avail() < 32) return *this;
    int n = snprintf(buffer_.current(), buffer_.avail(), "%.6g", v);
    if (n > 0 && static_cast<size_t>(n) < buffer_.avail()) {
        buffer_.add(n);
    }
    return *this;
}

LogStream& LogStream::operator<<(const char* v) {
    if (v) buffer_.append(v, strlen(v));
    else   buffer_.append("(null)", 6);
    return *this;
}

// ========== AsyncLogger 实现 ==========
AsyncLogger& AsyncLogger::getInstance() {
    static AsyncLogger instance;
    return instance;
}

AsyncLogger::AsyncLogger()
    : currentBuffer_(new Buffer),
      nextBuffer_(new Buffer),
      file_(stdout) {
}

AsyncLogger::~AsyncLogger() {
    stop();
}

void AsyncLogger::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;  // 已经启动
    }

    // 上次 stop() 可能已将 currentBuffer_/nextBuffer_ 移走并释放，
    // 重复 start() 前必须重建缓冲，否则 append() 会解引用空指针。
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!currentBuffer_) {
            currentBuffer_.reset(new Buffer);
        }
        if (!nextBuffer_) {
            nextBuffer_.reset(new Buffer);
        }
    }

    bgThread_ = std::thread(&AsyncLogger::bgThreadFunc, this);
}

void AsyncLogger::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;  // 已经停止
    }
    
    // 通知后台线程退出
    cond_.notify_all();
    
    if (bgThread_.joinable()) {
        bgThread_.join();
    }
    
    // 最后一次刷新队列中的残余数据
    BufferVector remainingBuffers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 将当前缓冲区也加入待写队列
        if (currentBuffer_ && currentBuffer_->length() > 0) {
            buffers_.push_back(std::move(currentBuffer_));
        }
        
        remainingBuffers.swap(buffers_);
    }
    
    // 写入残余数据
    for (const auto& buffer : remainingBuffers) {
        if (buffer && buffer->length() > 0) {
            writeToFile(buffer->data(), buffer->length());
        }
    }
    
    // 关闭文件并恢复默认 stdout 输出，保证单例 stop() 后可继续复用；
    // 否则 file_ 置空后再次 start() 的日志会在 writeToFile 中被静默丢弃。
    if (file_ && file_ != stdout) {
        fflush(file_);
        // P2: 开启 fsync 策略时，关闭前确保数据真正落盘
        if (fsyncOnFlush_.load(std::memory_order_relaxed)) {
            fsync(fileno(file_));
        }
        fclose(file_);
        file_ = stdout;
    }
}

void AsyncLogger::setOutputFile(const std::string& filename, size_t rollSize) {
    // 运行中禁止切换输出文件：后台线程在锁外持有并写入 file_，
    // 运行期 fclose/fopen 会与其形成数据竞争（use-after-close）。
    // 配置必须发生在 start() 之前。
    if (running_.load(std::memory_order_acquire)) {
        fprintf(stderr, "[AsyncLogger] setOutputFile() ignored: must be called before start()\n");
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    
    // 关闭旧文件
    if (file_ && file_ != stdout) {
        fclose(file_);
        file_ = nullptr;
    }
    
    filename_ = filename;
    rollSize_ = rollSize;
    
    if (filename_.empty()) {
        file_ = stdout;
        currentFileWritten_ = 0;
    } else {
        file_ = fopen(filename_.c_str(), "a");
        if (!file_) {
            fprintf(stderr, "[AsyncLogger] Cannot open log file: %s, fallback to stdout\n", 
                    filename_.c_str());
            file_ = stdout;
            currentFileWritten_ = 0;
        } else {
            // 获取当前文件大小
            fseeko(file_, 0, SEEK_END);
            currentFileWritten_ = static_cast<size_t>(ftello(file_));
        }
    }
}

AsyncLogger::Stats AsyncLogger::getStats() const {
    Stats stats;
    stats.totalLogs = totalLogs_.load(std::memory_order_relaxed);
    stats.droppedLogs = droppedLogs_.load(std::memory_order_relaxed);
    stats.bytesWritten = bytesWritten_.load(std::memory_order_relaxed);
    
    std::lock_guard<std::mutex> lock(mutex_);
    stats.queuedBuffers = buffers_.size();
    
    return stats;
}

// ========== 前端：快速路径日志追加 ==========
void AsyncLogger::append(const char* logline, size_t len, LogLevel level) {
    // 级别过滤
    if (level < level_.load(std::memory_order_relaxed)) {
        return;
    }
    
    totalLogs_.fetch_add(1, std::memory_order_relaxed);

    // 单条日志超过缓冲区容量上限时截断，避免超长日志在切换缓冲区后
    // 仍放不下而被 FixedBuffer::append 静默丢弃。
    const size_t kMaxLineLen = kLargeBuffer - 1;
    if (len > kMaxLineLen) {
        len = kMaxLineLen;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    
    // Case 1: 当前缓冲区有足够空间（最常见路径）
    if (currentBuffer_->avail() > len) {
        currentBuffer_->append(logline, len);
    }
    // Case 2: 当前缓冲区空间不足，需要切换
    else {
        // 将满缓冲区加入队列
        buffers_.push_back(std::move(currentBuffer_));
        
        // 切换到备用缓冲区
        if (nextBuffer_) {
            currentBuffer_ = std::move(nextBuffer_);
        } else {
            // 备用缓冲区也没有了（极端情况），分配新缓冲区
            currentBuffer_.reset(new Buffer);
        }
        
        currentBuffer_->append(logline, len);
        
        // 通知后台线程有数据可写
        cond_.notify_one();
    }
}

// ========== 后台线程：批量写入磁盘 ==========
void AsyncLogger::bgThreadFunc() {
    // start() 后立即 stop() 时，本线程可能尚未被调度运行，
    // 此时 running_ 已为 false，直接退出即可（不可断言失败）。
    if (!running_.load(std::memory_order_acquire)) {
        return;
    }
    
    // 后台线程专用的备用缓冲区（用于复用）
    BufferPtr newBuffer1(new Buffer);
    BufferPtr newBuffer2(new Buffer);
    BufferVector buffersToWrite;
    buffersToWrite.reserve(16);

    // P2: 积压预警限频计数
    uint64_t backlogWarnCount = 0;
    
    while (running_.load(std::memory_order_acquire)) {
        assert(newBuffer1 && newBuffer1->length() == 0);
        assert(newBuffer2 && newBuffer2->length() == 0);
        assert(buffersToWrite.empty());
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            
            // 等待数据到来或超时
            if (buffers_.empty()) {
                cond_.wait_for(lock, std::chrono::seconds(flushInterval_.load(std::memory_order_relaxed)));
            }
            
            // 即使没有满缓冲区，也定期将当前缓冲区刷盘
            buffers_.push_back(std::move(currentBuffer_));
            currentBuffer_ = std::move(newBuffer1);
            
            buffersToWrite.swap(buffers_);
            
            // 补充备用缓冲区
            if (!nextBuffer_) {
                nextBuffer_ = std::move(newBuffer2);
            }
        }
        
        // 检查是否有缓冲区积压过多（背压控制）
        assert(!buffersToWrite.empty());

        // P2: 积压预警：消费速率跟不上产生速率时提示（限频打印，每 100 次输出一次）
        if (buffersToWrite.size() > 12) {
            if (backlogWarnCount++ % 100 == 0) {
                fprintf(stderr,
                        "[AsyncLogger] Backlog warning: %zu buffers queued, consumer falling behind\n",
                        buffersToWrite.size());
            }
        }
        
        if (buffersToWrite.size() > 25) {
            // 丢弃中间的普通日志缓冲区，但保留前2个和最后1个
            char buf[256];
            snprintf(buf, sizeof(buf), 
                     "[AsyncLogger] Dropped %zu log buffers due to overload\n",
                     buffersToWrite.size() - 2);
            fputs(buf, stderr);
            
            // 注意：droppedLogs_ 统计的是被丢弃的缓冲区个数（一个缓冲区可能含数万条日志），不是日志条数
            droppedLogs_.fetch_add(buffersToWrite.size() - 2, std::memory_order_relaxed);
            
            buffersToWrite.erase(buffersToWrite.begin() + 2, buffersToWrite.end() - 1);
        }
        
        // 批量写入磁盘
        for (const auto& buffer : buffersToWrite) {
            writeToFile(buffer->data(), buffer->length());
        }

        // P2: 可配置 fsync 持久化（默认关闭，性能优先）
        if (fsyncOnFlush_.load(std::memory_order_relaxed) && file_ && file_ != stdout) {
            fflush(file_);
            fsync(fileno(file_));
        }
        
        if (buffersToWrite.size() > 2) {
            buffersToWrite.resize(2);
        }
        
        // 复用缓冲区：归还给后台线程的备用池
        if (!newBuffer1) {
            assert(!buffersToWrite.empty());
            newBuffer1 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer1->reset();
        }
        
        if (!newBuffer2) {
            assert(!buffersToWrite.empty());
            newBuffer2 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer2->reset();
        }
        
        buffersToWrite.clear();
    }
    
    // 退出前最后一次 flush：stop() 唤醒本线程时，循环体内刚被交换出来
    // 但尚未落盘的缓冲区（buffersToWrite）需要在这里写掉，否则最后一批日志丢失。
    for (const auto& buffer : buffersToWrite) {
        writeToFile(buffer->data(), buffer->length());
    }
    buffersToWrite.clear();

    if (file_) {
        fflush(file_);
        // P2: 开启 fsync 策略时，退出前同样保证落盘
        if (fsyncOnFlush_.load(std::memory_order_relaxed) && file_ != stdout) {
            fsync(fileno(file_));
        }
    }
}

// ========== 文件写入与轮转 ==========
void AsyncLogger::writeToFile(const char* data, size_t len) {
    if (!file_ || len == 0) {
        return;
    }
    
    // 检查是否需要轮转
    if (file_ != stdout && 
        rollSize_ > 0 && 
        currentFileWritten_ + len > rollSize_) {
        rollFile();
    }
    
    // 写入文件
    size_t written = fwrite(data, 1, len, file_);
    if (written != len) {
        int err = ferror(file_);
        fprintf(stderr, "[AsyncLogger] Write error: %d, written=%zu, expected=%zu\n",
                err, written, len);
        clearerr(file_);
    }
    
    currentFileWritten_ += written;
    bytesWritten_.fetch_add(written, std::memory_order_relaxed);
}

void AsyncLogger::rollFile() {
    if (!file_ || file_ == stdout || filename_.empty()) {
        return;
    }
    
    // 关闭当前文件
    fflush(file_);
    fclose(file_);
    file_ = nullptr;
    
    // 生成带时间戳的归档文件名
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    
    struct tm tm_time;
    localtime_r(&t, &tm_time);
    
    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), "%Y%m%d-%H%M%S", &tm_time);
    
    std::string archiveName = filename_ + "." + timeBuf;
    
    // 重命名当前日志文件
    if (rename(filename_.c_str(), archiveName.c_str()) != 0) {
        fprintf(stderr, "[AsyncLogger] Failed to rename %s to %s\n",
                filename_.c_str(), archiveName.c_str());
    }
    
    // 重新打开日志文件（新文件）
    file_ = fopen(filename_.c_str(), "a");
    if (!file_) {
        fprintf(stderr, "[AsyncLogger] Failed to reopen log file: %s, fallback to stdout\n",
                filename_.c_str());
        file_ = stdout;
    }
    
    currentFileWritten_ = 0;
}

} // namespace common
} // namespace tinymq
