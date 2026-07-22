# Changelog

本项目的所有重要更改都将记录在此文件中。

格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/)，
并且本项目遵循 [语义化版本](https://semver.org/lang/zh-CN/) 规范。

## [Unreleased] (待发布)
- 计划：引入 WAL (Write-Ahead Logging) 预写日志模块，实现数据持久化。
- 计划：实现 SSTable 刷盘与二进制序列化格式。

## [0.1.0] - 2026-07-22
### Added (新增)
- **核心数据结构**：从零手写实现了线程安全的 SkipList（跳表），支持 $O(\log N)$ 的插入、查询和删除操作。
- **KV 引擎接口**：封装了 `KVEngine` 对外接口，提供 `Put`, `Get`, `Erase` 方法。
- **并发控制**：在引擎层预留了 `std::mutex` 互斥锁，为未来的多线程并发读写打下基础。
- **调试工具**：实现了跳表的多层级可视化打印功能 (`display`)，方便直观查看内部索引结构。
- **工程化构建**：引入 CMake 构建系统，支持跨平台标准化编译。
- **版本控制**：初始化 Git 仓库，配置 SSH 协议并成功推送至 GitHub。
- **项目规范**：添加 `.gitignore` 文件，自动忽略 `build/` 等编译中间产物。

### Changed (变更)
- 优化了跳表随机层级生成算法，使用 `static std::mt19937` 避免每次插入时重复初始化随机数引擎，降低 CPU 开销。
- 将锁机制从底层数据结构 (`SkipList`) 剥离，上移至业务层 (`KVEngine`)，遵循单一职责原则，提升架构灵活性。

### Fixed (修复)
- 修复了模板类分文件编写时，因重复定义 `display()` 函数导致的编译重载错误。
- 规范了 C++ 单参数构造函数的 `explicit` 声明，防止编译器发生隐式类型转换。



