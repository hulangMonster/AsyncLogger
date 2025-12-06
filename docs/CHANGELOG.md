# 更新日志 CHANGELOG

所有显著变更都将记录在此文件中。

格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/)，本项目遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

***

## [1.1.0] - 2026-09-16

### 性能调优 (Performance Tuning)

- ⚡ 自研栈上定长缓冲 `LogStream` 替代 `ostringstream`：零堆分配、零 iostream 虚调用（原每条日志 2 次堆分配 + 5~8 次虚调用 + 1 次冗余拷贝）
- ⚡ 秒级时间戳缓存（thread_local）：`localtime_r`/`strftime` 从每条日志一次降为每秒一次
- ⚡ 实测提升（VM 同环境）：单线程 1.19 万 → 20~40 万条/秒（约 20~30 倍）、10 线程 P50 延迟 110μs → ~1.6μs、50 线程 3.4 万 → ~57~60 万条/秒

### 新增 (Added)

- ✨ `setFsyncOnFlush(bool)`：可配置 fsync 持久化策略（默认关闭；开启后每次后台 flush 强制落盘，防崩溃/断电丢失）
- ✨ 队列积压预警：待写队列超过 12 个缓冲时输出限频告警
- ✨ Benchmark 6：纯前端吞吐测试（输出到 /dev/null，隔离磁盘验证前端非瓶颈）

### 修复 (Fixed)

- 🐛 benchmark 统计口径：Benchmark 2/4 吞吐改用本组实际条数（原跨组累计虚高 20%~95%），字节数改用当组增量

### 文档 (Documentation)

- 📝 新增 `docs/PERFORMANCE_TUNING.md` 性能调优设计文档
- 📝 README / docs 性能数据更新为本机实测

***

## [1.0.0] - 2026-08-09

### 新增 (Added)

- ✨ 实现基于 muduo 设计的双缓冲异步日志系统
- ✨ 支持多线程并发日志写入
- ✨ 支持日志级别过滤（DEBUG/INFO/WARN/ERROR/FATAL）
- ✨ 支持文件自动轮转（按大小）
- ✨ 支持输出到文件或 stdout
- ✨ 提供统计信息接口（总日志数、丢弃数、写入字节数）
- ✨ 智能背压控制，防止内存溢出
- ✨ 缓冲区预分配和复用机制
- ✨ 流式日志接口（LOG_INFO << "message"）
- ✨ 自动时间戳和文件位置记录

### 性能特性 (Performance)

- ⚡ 前端快速路径延迟 < 1 微秒
- ⚡ 吞吐量 > 100 万条日志 / 秒
- ⚡ 批量写入磁盘，减少系统调用
- ⚡ 零拷贝缓冲区交换
- ⚡ 内存占用 < 20MB（正常情况）

### 文档 (Documentation)

- 📝 完整的 README.md 使用文档
- 📝 详细的 DESIGN.md 设计文档
- 📝 全面的 FAQ.md 常见问题
- 📝 示例代码 example.cpp
- 📝 性能基准测试 benchmark.cpp
- 📝 单元测试 unit_test.cpp

### 构建系统 (Build)

- 🔧 支持 CMake 构建
- 🔧 支持 Makefile 构建
- 🔧 支持 Linux Shell 脚本

### 示例和测试 (Examples & Tests)

- ✅ 5 个使用示例（基本、多线程、文件、压力、过滤）
- ✅ 5 个性能基准测试（单线程、多线程、消息长度、压力、过滤）
- ✅ 10 个单元测试（覆盖核心功能）
- ✅ 性能对比脚本（异步 vs 同步）

***

## [未来计划] - Roadmap

### [1.1.0] - 计划中

- [ ] 添加多 Sink 支持（同时输出到多个目标）
- [ ] 支持日志归档压缩（gzip）
- [ ] 添加日志采样功能（高频事件采样）
- [ ] 支持动态调整日志级别（无需重启）
- [ ] 添加日志统计面板（Web UI）

### [1.2.0] - 计划中

- [ ] 支持结构化日志（JSON 格式）
- [ ] 添加自定义日志格式支持
- [ ] 支持日志上下文（Context）
- [ ] 添加日志过滤器（Filter）
- [ ] 支持异步日志回调

### [1.3.0] - 计划中

- [ ] 添加网络 Sink（发送到远程日志服务器）
- [ ] 支持日志加密
- [ ] 添加日志签名（防篡改）
- [ ] 支持日志索引（快速搜索）
- [ ] 集成 Prometheus metrics

### [2.0.0] - 长期计划

- [ ] 使用无锁队列替代 mutex
- [ ] SIMD 优化（加速 memcpy）
- [x] 零拷贝优化 → ✅ 已完成（1.1.0：自研 LogStream 零堆分配）
- [ ] 支持 io_uring（Linux 5.1+）
- [ ] 添加内存池管理
- [ ] 支持日志分片（Sharding）
- [ ] 添加日志聚合功能

***

## 版本说明

### 版本号格式：MAJOR.MINOR.PATCH

- **MAJOR**：不兼容的 API 变更
- **MINOR**：向后兼容的功能新增
- **PATCH**：向后兼容的问题修复

### 变更类型标识

- `新增 (Added)`：新功能
- `变更 (Changed)`：现有功能的变化
- `废弃 (Deprecated)`：即将移除的功能
- `移除 (Removed)`：已删除的功能
- `修复 (Fixed)`：错误修复
- `安全 (Security)`：安全相关修复

***

## 贡献指南

如果你想贡献代码或报告问题：

1. Fork 本仓库
2. 创建功能分支 (`git checkout -b feature/AmazingFeature`)
3. 提交变更 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 创建 Pull Request

***

## 许可证

本项目采用 MIT 许可证 - 详见 LICENSE 文件

***

## 致谢

本项目设计灵感来源于：

- [muduo](https://github.com/chenshuo/muduo) - 陈硕的高性能网络库
- [spdlog](https://github.com/gabime/spdlog) - 快速 C++ 日志库
- [glog](https://github.com/google/glog) - Google 日志库

感谢所有开源贡献者的无私奉献！
