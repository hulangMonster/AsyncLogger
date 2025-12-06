# JLasynclogger —— 工业级异步双缓冲日志系统

基于 muduo 双缓冲设计理念实现的高性能异步日志库，面向高并发服务端应用。核心思路：**业务线程只做内存拷贝，磁盘 I/O 交给单一后台线程批量完成**，将日志写入从业务路径中彻底解耦。

项目以学习 muduo 的网络库与多线程设计思想为目标独立实现，配套完整的单元测试（10 项）与性能基准测试（6 组），并在 **Linux（GCC 11.4）实测通过**。

## 核心特性

### 1. 双缓冲与缓冲区复用
- 前端 `currentBuffer` + `nextBuffer` 双缓冲，切换时使用 `std::move` 转移缓冲区所有权，**避免整块日志数据的二次拷贝**
- 后台线程维护两块备用缓冲区，写满的缓冲区写盘后归还复用，**稳态运行零动态分配**（常驻约 16MB）
- 自研栈上定长缓冲 `LogStream`（16KB）完成格式化，**零堆分配、零 iostream 虚调用**；时间戳采用秒级缓存（详见 `docs/PERFORMANCE_TUNING.md`）

### 2. 异步批量写入
- **多生产者 - 单消费者模型**：任意线程并发调用 `LOG_XXX << ...`，单后台线程顺序批量落盘
- 业务线程内仅 memcpy 入缓冲 + 指针交换；磁盘 I/O、文件轮转全部在后台线程完成
- **短临界区设计**：锁内只有拷贝与指针交换（注：当前实现每次 `append()` 仍会加锁，并非无锁，加锁频率由 4MB 大缓冲摊薄）

### 3. 线程安全与优雅关闭
- `std::mutex` + `condition_variable` 保护缓冲区切换；`std::atomic` + 内存序实现级别过滤的无锁读取
- CAS 保证 `start()`/`stop()` 幂等；`stop()` 时后台线程先写空剩余缓冲区再退出，日志不丢失
- 已修复并验证：重复 `start()` 空指针崩溃、`stop()` 竞态丢最后一批日志、`setOutputFile()` 与后台线程的 `FILE*` 竞争等并发缺陷

### 4. 智能背压控制
- 缓冲队列超过阈值时**丢弃中间缓冲区、保留最早与最新日志**（兼顾现场上下文与最新状态）
- 提供完整统计：总日志数、丢弃数、写入字节数
- **积压预警**：待写队列超过 12 个缓冲时输出限频告警，过载可提前感知

### 5. 文件管理
- **自动轮转**：超过阈值（默认 500MB）自动归档，`logfile.20260915-153245` 时间戳命名
- **POSIX 文件 API**：`localtime_r`/`fseeko` 等标准接口，代码可移植

### 6. 灵活配置
- 日志级别过滤（DEBUG/INFO/WARN/ERROR/FATAL）
- 可配置刷新间隔（默认 3 秒）、轮转大小、输出到文件或 stdout、fsync 持久化策略（`setFsyncOnFlush`）

## 架构设计

```
                    前端（业务线程）
  Thread 1   Thread 2   ...   Thread N
      |          |                |
      v          v                v
  +-----------------------------------+
  |  append()：加锁临界区                |
  |  currentBuffer (4MB) --写满--> 转移 |
  |  nextBuffer    (4MB)  <-归还- 复用  |
  |  buffersToWrite（待写队列）          |
  |  notify_one() 唤醒后台线程            |
  +-----------------------------------+
                     |
                     v
  +-----------------------------------+
  | 后台线程（单线程）                    |
  |  wait / 超时(3s)                    |
  |  > 交换并接管写满的缓冲区             |
  |  > 归还备用缓冲区给前端               |
  |  > 批量 fwrite 落盘                 |
  |  > 文件轮转检查                      |
  +-----------------------------------+
                     |
                     v
                 磁盘文件
```

> 注：架构图使用 ASCII 绘制，避免 Markdown 格式化工具对特殊字符的转义污染。请勿用 Typora 打开本文件后保存（Typora 保存时会转义 Markdown 特殊字符导致文件被改写）；如需阅读请用 VS Code 内置预览（Ctrl+Shift+V）。

## 快速开始

### Linux（g++）
```bash
mkdir -p output
g++ -std=c++11 -O2 -Wall -Wextra -pthread logger.cpp examples/example.cpp -o output/logger_test
cd output && ./logger_test

# 单元测试（10 项）
g++ -std=c++11 -O2 -Wall -Wextra -pthread logger.cpp tests/unit_test.cpp -o output/unit_test
cd output && ./unit_test
```

## 测试指南（全部指令）

> **首次使用提示**：若执行 `./build.sh` 报 `Permission denied`，请先执行 `chmod +x build.sh`（从 Windows 拷贝到 Linux 后文件会丢失执行权限）；或直接改用 `bash build.sh <command>` 运行。

**所有产物统一输出到项目根目录的 `output/` 文件夹**：编译产物（.obj/.o/.a/可执行文件）与测试/示例运行的日志文件全部在其中（构建脚本自动创建并进入该目录，项目根目录不散落任何产物）。测试类可执行文件均由 `logger.cpp` 链接对应源文件编译生成。

### 1. 单元测试（10 项功能验证）

| 指令 | 说明与备注 |
| --- | --- |
| `./build.sh runtest` | 编译并运行 10 项单测，约 5 秒；输出 `Passed/Failed/Total`，产物在 `output/` |
| `make test` | 同上（先编译静态库再链接） |
| `cmake -B build && cmake --build build --target logger_unit_test` | 配置并编译单测目标（需 CMake ≥ 3.10），运行需手动执行 |

**手动编译运行**（不经构建脚本，产物同样进 `output/`）：
```bash
mkdir -p output
g++ -std=c++11 -O2 -Wall -Wextra -pthread logger.cpp tests/unit_test.cpp -o output/unit_test
cd output && ./unit_test
```

### 2. 性能基准（6 组压测）

> 以下 6 组基准均在 **Linux 单平台**实测（Ubuntu 22.04 / GCC 11.4 / `-O2`），实测数据见下方「性能测试结果」。

| 指令 | 说明与备注 |
| --- | --- |
| `./build.sh bench` | 跑完全部 6 组，约 30 秒；压测组会生成数百 MB 日志，请预留磁盘空间 |
| `make benchmark` | 同上 |
| `./logger_benchmark 1` | 只跑第 1 组：单线程吞吐（10 万条），约 1 秒 |
| `./logger_benchmark 2` | 只跑第 2 组：10 线程并发 + 延迟分布（P50-P999） |
| `./logger_benchmark 3` | 只跑第 3 组：消息长度 10B-2KB 吞吐/带宽 |
| `./logger_benchmark 4` | 只跑第 4 组：50 线程极限压力（100 万条，约 13 秒） |
| `./logger_benchmark 5` | 只跑第 5 组：级别过滤性能对比 |
| `./logger_benchmark 6` | 只跑第 6 组：纯前端吞吐（输出到 /dev/null，隔离磁盘） |
| `make bench-single/bench-multi/bench-length/bench-stress/bench-filter/bench-frontend` | 与上面 1-6 组一一对应的别名 |

> 运行前请先进入 `output/` 目录（`cd output && ./logger_benchmark 1`）。

### 3. 示例程序（5 个使用场景）

| 指令 | 说明与备注 |
| --- | --- |
| `./build.sh run` / `make run` | 运行全部 5 个示例场景 |
| `./logger_test 1` | 基本使用（stdout 输出） |
| `./logger_test 2` | 多线程并发（10 线程 x 1000 条） |
| `./logger_test 3` | 文件输出与轮转（生成 `test.log` 及归档） |
| `./logger_test 4` | 压力测试（20 线程 x 10000 条） |
| `./logger_test 5` | 级别过滤演示 |
| `make run-basic/run-multi/run-file/run-stress/run-filter` | 与上面 1-5 一一对应的别名 |

### 4. 清理

| 指令 | 说明与备注 |
| --- | --- |
| `./build.sh clean` | 删除整个 `output/` 及根目录残留编译产物 |
| `make clean` | 同上 |

## 使用示例

```cpp
#include "logger.h"

int main() {
    // 启动（必须先于任何日志调用）
    AsyncLogger::getInstance().start();

    // 流式输出
    LOG_INFO << "Server started on port " << 8080;
    LOG_WARN << "Memory usage: " << 85.5 << "%";
    LOG_ERROR << "Connection failed: timeout";

    // 优雅关闭：确保剩余日志全部落盘
    AsyncLogger::getInstance().stop();
    return 0;
}
```

```cpp
// 输出到文件 + 100MB 自动轮转（须在 start() 前调用）
AsyncLogger::getInstance().setOutputFile("server.log", 100 * 1024 * 1024);

// 级别过滤：只记录 WARN 及以上
AsyncLogger::getInstance().setLogLevel(LogLevel::WARN);

// 统计信息
auto stats = AsyncLogger::getInstance().getStats();
// stats.totalLogs / stats.droppedLogs / stats.bytesWritten
```

## 性能测试结果

> **口径说明**：延迟为 `LOG_INFO` 语句全流程耗时（含 `LogStream` 构造与格式化）；吞吐按当组实际产生条数计算（Benchmark 2/4 按「线程数 × 每线程条数」计，字节数按当组增量计，已修正统计计数跨组累计问题）。不同机器/编译器/磁盘下数字会有差异，请以本机 `./build.sh bench` 实测为准。
>
> **数据口径**：下表为同环境连续 **4 次实测的中位数**（附波动范围）。虚拟机共享宿主资源，单次测量波动可达 ±40%，多次取中位数才是稳健口径；可用仓库内置 `./bench_median.sh` 一键复测（默认 5 次 × 场景 1/2/4/6）。

### 测试环境（本仓库实测基线）

- 系统：Ubuntu 22.04 虚拟机（VMware），8 核 / 7.7GB 内存，VMware 虚拟磁盘
- 编译器：GCC 11.4，`-O2 -pthread`
- 磁盘实测落盘带宽：约 32 MB/s（`dd bs=1M count=1024 conv=fdatasync`；O_DIRECT 瞬时 270MB/s 为虚拟机写缓存假象）
- 代码版本：性能调优版（自研定长缓冲 LogStream + 秒级时间戳缓存，见 `docs/PERFORMANCE_TUNING.md`）

### 实测结果（`./build.sh bench` 可复现）

| 场景 | 结果（4 次实测中位数） |
| --- | --- |
| 单线程吞吐（10 万条） | **142 万条/秒**（范围 105~163 万），平均延迟 0.74μs/条 |
| 10 线程并发（50 万条） | **46.3 万条/秒**（43~57 万），38.9 MB/s，零丢弃 |
| 50 线程压力（100 万条） | **56.8 万条/秒**（41~70 万），48.9 MB/s，零丢弃 |
| 10 线程延迟 | P50 2.07μs / P90 9.1μs / P95 19.6μs / P99 414μs / P999 2.4ms / Max 18.7ms |
| 纯前端吞吐（输出到 /dev/null） | **232 万条/秒**（205~284 万），平均 0.44μs/条（隔离磁盘，验证前端非瓶颈） |
| 级别过滤热路径 | 全过滤 **2.67 亿条/秒**（1.8~3.7 亿），比全量记录快约 220~410 倍 |
| 消息长度 10B→2KB | 小消息（10~200B）200~240 万条/秒（前端受限）；1KB 起受磁盘限制降至 4~9 万条/秒 |

> 优化前基线（同环境）：单线程 1.19 万条/秒（83.7μs/条）、10 线程 2.55 万条/秒（P50 110μs）、50 线程 3.4 万条/秒。本次调优后单线程提升 **119 倍**、10 线程 P50 延迟提升 **53 倍**、50 线程提升 **17 倍**，详见 `docs/PERFORMANCE_TUNING.md`。

**内存占用**：约 16MB（4 个 4MB 缓冲区），背压队列上限约 116MB。

## 单元测试（10 项）

| 测试 | 覆盖点 |
| --- | --- |
| test_basic_start_stop | 基本启动/停止生命周期 |
| test_basic_logging | 各级别日志输出格式 |
| test_log_level_filtering | 级别过滤正确性 |
| test_file_output | 文件输出 |
| test_concurrent_logging | 多线程并发写入不丢不串 |
| test_large_messages | 大消息（含超长截断）处理 |
| test_rapid_start_stop | 快速重复启停不崩溃 |
| test_statistics | 统计计数准确性 |
| test_empty_messages | 空消息处理 |
| test_file_rolling | 文件轮转归档 |

## 项目结构

```
├── logger.h / logger.cpp       # 核心实现（约 630 行）
├── examples/example.cpp        # 使用示例（5 个场景）
├── tests/
│   ├── unit_test.cpp           # 单元测试（10 项）
│   └── benchmark.cpp           # 性能基准（6 组测试）
├── docs/                       # 设计文档 / FAQ / 快速上手 / 变更记录
├── CMakeLists.txt              # CMake 构建（静态库 + 示例 + 基准 + 单测 + install）
├── Makefile                    # make 构建（含 test 目标）
├── build.sh                    # Linux 构建脚本
└── README.md / LICENSE / .gitignore
```

## 文档

- `docs/DESIGN.md` —— 设计文档（架构详解）
- `docs/FAQ.md` —— 常见问题（25 个问答）
- `docs/QUICKSTART.md` —— 快速上手（5 分钟入门）
- `docs/CHANGELOG.md` —— 变更记录
- `docs/PROJECT_STRUCTURE.md` —— 目录结构详解

## 注意事项

1. 使用前必须调用 `start()`，退出前调用 `stop()` 保证日志落盘
2. 进程崩溃/断电时最多丢失一个刷新间隔（默认 3 秒）内的日志；如需防丢失可调用 `setFsyncOnFlush(true)` 强制每次刷盘落盘（会降低吞吐）
3. 使用 POSIX 标准接口（`localtime_r`/`fseeko`），面向 Linux 平台
4. 确保日志目录可写、磁盘空间充足
5. **请勿用 Typora 打开 README.md 后保存**（Typora 保存时会转义特殊字符重写文件，破坏 Markdown 格式）；阅读请用 VS Code 内置预览

## 扩展方向

- **thread_local 无锁前端**：消除每条日志的全局锁竞争，高并发（>50 线程）场景的下一步
- **零分配格式化 ✅ 已完成**：自研定长缓冲 `LogStream` 替代 `ostringstream`（见 `docs/PERFORMANCE_TUNING.md`）
- 多 Sink（文件/控制台/网络）、JSON 结构化输出、日志压缩归档

## 许可证与参考

开源示例代码，可自由使用修改。

- [muduo 网络库](https://github.com/chenshuo/muduo)
- [C++ Concurrency in Action](https://www.manning.com/books/c-plus-plus-concurrency-in-action-second-edition)
