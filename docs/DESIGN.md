/**
 * @file design_doc.md
 * @brief 异步双缓冲日志系统设计文档
 */

# 异步双缓冲日志系统设计文档

## 1. 设计目标

### 1.1 功能目标
- 支持多线程并发日志写入
- 异步写入磁盘，不阻塞业务线程
- 支持日志级别过滤
- 支持文件自动轮转
- 提供统计信息

### 1.2 性能目标
- 前端日志记录延迟 < 1 微秒（✅ 已实测：纯前端 0.22~0.39μs，输出到 /dev/null 时）
- 吞吐量 > 100 万条日志/秒（✅ 纯前端已实测 257~448 万条/秒；落盘链路受磁盘带宽限制，VM 实测 20~60 万条/秒）
- 内存占用 < 20MB
- CPU 占用 < 5%（正常负载）

### 1.3 可靠性目标
- 线程安全，无数据竞争
- 优雅关闭，确保日志不丢失
- 背压控制，防止内存溢出
- 错误处理，文件操作失败时降级

## 2. 架构设计

### 2.1 整体架构

```
┌──────────────────────────────────────────────────────────┐
│                   前端（多线程）                            │
│   ┌────────┐  ┌────────┐  ┌────────┐       ┌────────┐   │
│   │Thread 1│  │Thread 2│  │Thread 3│  ...  │Thread N│   │
│   └────┬───┘  └────┬───┘  └────┬───┘       └────┬───┘   │
│        │           │           │                 │        │
│        └───────────┴───────────┴─────────────────┘        │
│                          │                                 │
│                          ▼                                 │
│              ┌───────────────────────┐                     │
│              │   append() 加锁区域    │                     │
│              ├───────────────────────┤                     │
│              │ currentBuffer (4MB)   │ ◄── 当前写入        │
│              │ nextBuffer    (4MB)   │ ◄── 备用缓冲        │
│              │ buffers_   (队列)     │ ◄── 满缓冲队列      │
│              └───────────┬───────────┘                     │
│                          │                                 │
│                          │ condition_variable::notify      │
└──────────────────────────┼─────────────────────────────────┘
                           │
                           ▼
┌──────────────────────────────────────────────────────────┐
│                  后台线程（单线程）                         │
│              ┌───────────────────────┐                     │
│              │  等待通知或超时 (3s)   │                     │
│              └───────────┬───────────┘                     │
│                          │                                 │
│              ┌───────────▼───────────┐                     │
│              │ 1. 交换缓冲区队列      │                     │
│              │ 2. 归还备用缓冲区      │                     │
│              └───────────┬───────────┘                     │
│                          │                                 │
│              ┌───────────▼───────────┐                     │
│              │ 3. 批量写入磁盘        │                     │
│              │ 4. 检查文件轮转        │                     │
│              │ 5. 回收缓冲区          │                     │
│              └───────────┬───────────┘                     │
│                          │                                 │
│                          ▼                                 │
│                    ┌─────────┐                             │
│                    │ 磁盘文件 │                             │
│                    └─────────┘                             │
└──────────────────────────────────────────────────────────┘
```

### 2.2 核心组件

#### 2.2.1 FixedBuffer
- 固定大小的字符缓冲区（默认 4MB）
- 提供 append、reset、length、avail 等操作
- 栈上分配，避免动态内存分配

#### 2.2.2 LogStream
- RAII 日志构造器
- 支持流式输出（operator<<）
- 析构时自动提交到 AsyncLogger

#### 2.2.3 AsyncLogger
- 单例模式
- 管理双缓冲和后台线程
- 提供配置接口（级别、文件、轮转）

## 3. 关键技术

### 3.1 双缓冲机制

#### 3.1.1 设计思想
- **前端双缓冲**：currentBuffer + nextBuffer
- **后台缓冲池**：newBuffer1 + newBuffer2
- **缓冲区复用**：写完的缓冲区归还给前端

#### 3.1.2 工作流程
```cpp
// 前端写入（快速路径）
if (currentBuffer->avail() > len) {
    currentBuffer->append(logline, len);  // 无需切换，直接返回
}

// 前端写入（需要切换）
else {
    buffers.push_back(std::move(currentBuffer));  // 当前缓冲区满，加入队列
    currentBuffer = std::move(nextBuffer);        // 切换到备用缓冲区
    currentBuffer->append(logline, len);
    cond.notify_one();                            // 通知后台线程
}

// 后台线程处理
{
    std::unique_lock lock(mutex);
    cond.wait_for(lock, 3s, [&]{ return !buffers.empty(); });
    
    buffers.push_back(std::move(currentBuffer));  // 连当前缓冲区也取走
    currentBuffer = std::move(newBuffer1);         // 归还新的空缓冲区
    
    if (!nextBuffer) {
        nextBuffer = std::move(newBuffer2);        // 补充备用缓冲区
    }
    
    buffersToWrite.swap(buffers);                  // 交换，最小化持锁时间
}

// 写入磁盘（锁外，避免持锁做 I/O）
for (auto& buffer : buffersToWrite) {
    fwrite(buffer->data(), 1, buffer->length(), file);
}

// 回收缓冲区
if (!newBuffer1) {
    newBuffer1 = std::move(buffersToWrite.back());
    newBuffer1->reset();
}
```

### 3.2 短临界区优化

> **说明**：当前实现每次 `append` 仍需获取全局互斥锁，并不存在真正无锁的快速路径；优化点在于临界区极短（仅一次 memcpy 或指针交换）+ 4MB 大缓冲把队列操作频率降至每 4MB 一次。真正的前端无锁化（thread_local 分片缓冲 / 无锁 MPSC 队列）留作后续优化方向。

#### 3.2.1 快速路径（加锁，但临界区极短）
- 绝大多数日志写入场景：currentBuffer 有空间
- 临界区内仅一次 memcpy，持锁时间极短
- 延迟目标 < 1 微秒（✅ 纯前端实测 0.22~0.39μs；落盘路径 2.6~4.2μs）

#### 3.2.2 慢速路径
- 少数场景：需要切换缓冲区
- 持锁时间 < 10 微秒（仅指针交换）
- 避免在锁内进行 I/O 操作

### 3.3 内存管理

#### 3.3.1 预分配策略
```
总内存占用 ≈ 4 个缓冲区 × 4MB = 16MB

- currentBuffer   (4MB) - 前端当前写入
- nextBuffer      (4MB) - 前端备用
- newBuffer1      (4MB) - 后台备用1
- newBuffer2      (4MB) - 后台备用2
```

#### 3.3.2 缓冲区复用
```cpp
// 循环使用，避免频繁 new/delete
Buffer lifecycle:
  前端写满 → 后台写盘 → reset() → 归还前端 → 再次使用
```

### 3.4 背压控制

#### 3.4.1 问题场景
- 日志产生速度 > 磁盘写入速度
- 缓冲区队列无限增长
- 内存溢出风险

#### 3.4.2 解决方案
```cpp
if (buffers.size() > 25) {
    // 保留前 2 个和最后 1 个，丢弃中间部分
    buffers.erase(buffers.begin() + 2, buffers.end() - 1);
    droppedLogs += (dropped_count);
}
```

### 3.5 文件轮转

#### 3.5.1 轮转触发条件
```cpp
if (currentFileSize + bufferSize > rollSize) {
    rollFile();
}
```

#### 3.5.2 轮转流程
```cpp
1. fclose(current_file)
2. rename("app.log", "app.log.20260809-153245")
3. fopen("app.log", "a")  // 创建新文件
4. currentFileSize = 0
```

## 4. 性能分析

### 4.1 时间复杂度

| 操作 | 时间复杂度 | 说明 |
|------|-----------|------|
| 前端 append（快速路径） | O(n) | n = 日志长度，memcpy |
| 前端 append（慢速路径） | O(1) | 指针交换 + notify |
| 后台批量写入 | O(m) | m = 缓冲区总大小 |
| 级别过滤 | O(1) | 原子变量加载 |

### 4.2 空间复杂度

| 组件 | 空间占用 | 说明 |
|------|---------|------|
| FixedBuffer | 4MB × 4 | 双缓冲 + 后台备用 |
| buffers_ 队列 | 动态 | 最多 25 个缓冲区 (100MB) |
| 其他开销 | < 1MB | 互斥锁、线程栈等 |
| **总计** | **16MB - 116MB** | 正常 16MB，极端 116MB |

### 4.3 性能瓶颈

#### 4.3.1 磁盘 I/O
- **瓶颈**：机械硬盘 IOPS 限制
- **优化**：批量写入，减少系统调用
- **效果**：10 次 fwrite → 1 次 fwrite

#### 4.3.2 锁竞争
- **瓶颈**：多线程竞争 mutex
- **优化**：双缓冲减少锁频率
- **效果**：每 4MB 才加锁一次

#### 4.3.3 内存分配
- **瓶颈**：频繁 new/delete
- **优化**：缓冲区复用
- **效果**：几乎零动态分配

## 5. 对比分析

### 5.1 与原实现对比

| 特性 | 原实现 | 新实现 | 改进 |
|------|--------|--------|------|
| 缓冲区设计 | thread_local 单缓冲 | 全局双缓冲 | ✓ 减少内存占用 |
| 内存分配 | 频繁 new/delete | 预分配 + 复用 | ✓ 10x 性能提升 |
| 锁粒度 | 每次切换都加锁 | 仅切换时加锁 | ✓ 减少竞争 |
| 背压控制 | 丢弃整个缓冲区 | 智能丢弃中间部分 | ✓ 更合理 |
| 统计信息 | 仅丢弃计数 | 完整统计 | ✓ 可观测性 |

### 5.2 与其他日志库对比

#### spdlog
- **优势**：功能更丰富（多 sink、格式化）
- **劣势**：复杂度更高
- **适用场景**：需要灵活配置

#### glog
- **优势**：Google 出品，稳定
- **劣势**：同步日志，性能一般
- **适用场景**：对性能要求不高

#### 本实现
- **优势**：极简、高性能、易集成
- **劣势**：功能相对简单
- **适用场景**：高性能服务器

## 6. 使用建议

### 6.1 配置建议

#### 6.1.1 日志级别
```cpp
// 开发环境
logger.setLogLevel(LogLevel::DEBUG);

// 生产环境
logger.setLogLevel(LogLevel::INFO);

// 紧急排查
logger.setLogLevel(LogLevel::WARN);
```

#### 6.1.2 文件轮转
```cpp
// 高流量服务：频繁轮转
logger.setOutputFile("app.log", 100 * 1024 * 1024);  // 100MB

// 低流量服务：按天轮转
logger.setOutputFile("app.log", 500 * 1024 * 1024);  // 500MB

// 超高流量：更小的文件
logger.setOutputFile("app.log", 50 * 1024 * 1024);   // 50MB
```

#### 6.1.3 刷新间隔
```cpp
// 实时性要求高
logger.setFlushInterval(1);  // 1 秒

// 吞吐量优先
logger.setFlushInterval(5);  // 5 秒
```

### 6.2 最佳实践

#### 6.2.1 避免在日志中进行复杂计算
```cpp
// ❌ 不好：每次都计算
LOG_INFO << "Hash: " << computeExpensiveHash(data);

// ✅ 更好：先判断级别
if (logger.getLogLevel() <= LogLevel::INFO) {
    auto hash = computeExpensiveHash(data);
    LOG_INFO << "Hash: " << hash;
}
```

#### 6.2.2 避免记录敏感信息
```cpp
// ❌ 危险
LOG_INFO << "Password: " << password;

// ✅ 安全
LOG_INFO << "User authenticated: " << username;
```

#### 6.2.3 使用合适的日志级别
```cpp
LOG_DEBUG << "Function entry: calculateSum()";        // 调试信息
LOG_INFO << "Server started on port 8080";            // 重要事件
LOG_WARN << "Connection pool size: " << size;         // 警告
LOG_ERROR << "Failed to connect: " << error;          // 错误
LOG_FATAL << "Out of memory, exiting";                // 致命错误
```

## 7. 扩展方向

### 7.1 功能扩展

#### 7.1.1 多 Sink 支持
```cpp
logger.addSink(new FileSink("app.log"));
logger.addSink(new ConsoleSink(stdout));
logger.addSink(new NetworkSink("log-server:9000"));
```

#### 7.1.2 结构化日志
```cpp
LOG_INFO.field("user", "alice")
        .field("action", "login")
        .field("ip", "192.168.1.1")
        .json();
```

#### 7.1.3 日志采样
```cpp
// 仅记录 1% 的日志
LOG_INFO_SAMPLED(0.01) << "High frequency event";
```

### 7.2 性能优化

#### 7.2.1 无锁队列
- 使用 lock-free queue 替代 mutex + vector
- 进一步减少锁竞争

#### 7.2.2 SIMD 优化
- 使用 SIMD 指令加速 memcpy
- 提升大日志写入性能

#### 7.2.3 零分配格式化 ✅（1.1.0 已实现）
- 自研栈上定长缓冲 `LogStream` 替代 `ostringstream`（参考 muduo），零堆分配、零 iostream 虚调用
- 消除每条日志 2 次堆分配（ostringstream 构造 + `str()` 拷贝）与 1 次冗余全量拷贝
- 时间戳秒级缓存：`localtime_r` 每条日志一次 → 每秒一次
- 实现与收益详见 `docs/PERFORMANCE_TUNING.md`

## 8. 总结

### 8.1 核心优势
1. **高性能**：双缓冲 + 批量写入 + 缓冲区复用
2. **低延迟**：前端快速路径 < 1 微秒（✅ 纯前端实测 0.22~0.39μs；落盘路径 2.6~4.2μs）
3. **线程安全**：多生产者-单消费者模型
4. **易集成**：单头文件 + 单实现文件

### 8.2 适用场景
- 高并发服务器（Web、RPC）
- 实时系统（游戏、交易）
- 高吞吐系统（大数据、流处理）

### 8.3 不适用场景
- 单线程应用（过度设计）
- 嵌入式系统（内存受限）
- 需要立即刷盘的场景（安全审计）
