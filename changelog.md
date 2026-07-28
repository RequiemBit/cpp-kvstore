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


## [0.2.0] - 2026-07-22
### Added (新增)
- **WAL 预写日志模块**：实现 `WalLogger` 组件，支持将数据操作以二进制协议（定长 Header + 变长 Payload）追加写入磁盘。
- **崩溃恢复机制**：在 `KVEngine` 启动时自动读取并解析 WAL 日志文件，将未持久化的数据重新加载到内存 SkipList 中，实现断电/崩溃后的数据恢复。
- **数据完整性校验**：在日志写入时引入 CRC32 校验和，在恢复阶段进行严格比对，防止因磁盘损坏或写入中断导致的脏数据污染。
- **防截断保护**：在日志恢复过程中增加对残缺 Header 的检测，遇到系统突然断电导致的不完整记录时安全停止恢复，防止内存越界。
- **工程化测试**：编写崩溃模拟测试用例，验证了 `abort()` 强杀进程后重启，数据依然能够完整恢复。

### Changed (变更)
- **引擎初始化逻辑**：重构 `KVEngine` 构造函数，强制要求传入 WAL 文件路径，并将崩溃恢复逻辑从构造函数中剥离，使架构更加清晰。
- **写入流程升级**：`KVEngine` 的 `Put` 和 `Erase` 操作全面接入 WAL，严格遵循“先写日志，再写内存”的工业级存储引擎规范。

### Fixed (修复)
- 修复了日志恢复时由于文件指针错位导致的解析异常问题，确保二进制日志流能够无缝衔接读取。


## [0.3.0] - 2026-07-22
### Added (新增)
- **零拷贝数据视图**：引入工业级 `Slice` 类，作为底层数据在内存中的轻量级视图，仅持有指针与长度，避免不必要的内存分配与拷贝。
- **二进制安全支持**：`Slice` 原生支持包含 `\0` 的二进制数据，打破了传统 C 风格字符串的限制，为存储任意类型数据打下基础。
- **字典序比较机制**：在 `Slice` 内部实现了基于 `memcmp` 的极快字典序比较逻辑，并重载了全套关系运算符，完美契合存储引擎的排序需求。
- **专项集成测试**：编写了针对 `Slice` 与底层模块的集成测试用例，验证了空字符串、二进制数据等边界条件下的正确性。

### Changed (变更)
- **核心接口升级**：将底层日志模块 (`WalLogger`) 的写入接口参数从 `std::string` 替换为 `Slice`，全面拥抱零拷贝范式，显著提升数据交互性能。
- **架构解耦**：将数据视图 (`Slice`) 与数据持久化 (`WalLogger`) 彻底解耦，提升了代码的模块化程度与未来的可扩展性。


## [0.4.0] - 2026-07-23

### Added (新增)
- **SSTable 核心组件**：新增 `SSTableBuilder` 与 `SSTableReader` 类，完整实现了 LSM-Tree 架构中磁盘有序表（SSTable）的读写闭环。
- **Data Block 自动切分**：Builder 支持在内存中按配置阈值（如 128 Bytes）动态切分 Data Block，保证文件内部数据的紧凑性与有序性。
- **Index Block 与 Footer 机制**：实现了索引块的自动生成与固定长度 Footer（24 Bytes）的落盘，为 SSTable 提供了可靠的元数据“锚点”。
- **二分查找与随机读**：Reader 支持基于 Footer 定位索引，并通过二分查找算法在磁盘文件中实现极速的 Key 检索。
- **全量读写一致性验证**：新增专项集成测试，成功验证了 100 条有序数据的写入、随机读取以及不存在 Key 的边界处理逻辑。

### Changed (变更)
- **核心架构升级**：项目正式迈入 LSM-Tree 阶段三（MemTable 刷盘与 SSTable 文件设计），打通了从内存数据结构到磁盘持久化文件的核心链路。
- **构建系统同步**：更新 CMakeLists.txt 配置，将新增的 `sstable_builder.cpp` 等核心模块无缝接入编译流水线，支持外部构建（Out-of-source build）。


## [0.5.0] - 2026-07-23

### Added (新增)
- **核心引擎集成**：将 `SSTableBuilder` 与 `SSTableReader` 无缝集成至 `KVEngine`，打通了从内存到磁盘的完整读写链路。
- **自动刷盘机制 (Auto-Flush)**：实现了 MemTable 容量监控，当内存节点数达到阈值时，自动触发后台刷盘，生成有序的 SSTable 文件。
- **全量重启恢复 (Restart & Recovery)**：引擎启动时自动扫描并加载磁盘上已有的 SSTable 文件，结合 WAL 重放，实现完整的崩溃恢复闭环。
- **手动强制刷盘接口**：新增 `force_flush()` 方法，支持在特定场景下手动触发内存数据落盘。
- **全链路集成测试**：新增 Phase 3 集成测试用例，成功验证了跨内存与磁盘的混合查询、删除操作以及引擎重启后的数据一致性。

### Changed (变更)
- **架构里程碑**：项目正式完成 LSM-Tree 阶段三的核心目标，`KVEngine` 现已具备真正的磁盘持久化与海量数据支撑能力。
- **查询路径升级**：`Get` 操作现已支持 `MemTable -> SSTable` 的多级查找逻辑，为后续的覆盖写（Overwrite）和 Compaction 打下基础。


## [0.6.0] - 2026-07-24

### Added (新增)
- **WAL 轮转与生命周期管理**：实现了 WAL 文件的自动轮转（Log Rotation）与 GC 清理机制。当 MemTable 触发 Flush 时自动冻结并生成新日志文件，并在 SSTable 成功落盘后安全清理旧 WAL，保障磁盘空间可控。
- **崩溃恢复增强 (Crash Recovery)**：完善了重启恢复流程。引擎启动时自动识别最新 WAL 目录，解析未落盘的 WAL 二进制记录并重新 Replay 写入 MemTable，实现了完整的持久性（Durability）与崩溃无损恢复。
- **墓碑机制与跨层级覆盖 (Tombstone & Overwrite)**：引入 `ValueType::kTypeDeletion` 标记，将删除操作改为追加写。在点查（Get）路径中实现跨层遮蔽逻辑，一旦在高层（较新的 SSTable）命中 Tombstone 即刻阻断读取，彻底解决数据一致性（Consistency）问题。
- **多路归并迭代器体系 (MergingIterator)**：设计了统一的 Iterator 虚基类，并基于 `std::priority_queue`（小顶堆）实现了多路归并迭代器，支持将多个不同 SSTable 的 Iterator 进行归并，输出全局有序（Key 字典序升序、Sequence 降序）的数据流。
- **Major Compaction 与物理垃圾回收 (GC)**：利用 MergingIterator 实现数据去重与老旧版本清理。在 Major Compaction 过程中过滤已删除的 Tombstone（当确认低层无更旧数据时），真正释放物理磁盘空间，并原子化替换旧 SSTable 文件。
- **全链路集成测试套件**：新增包含 4 大核心 Case 的集成测试框架，全面覆盖内存/磁盘墓碑阻断与持久化、WAL 轮转与崩溃恢复、以及 SSTable 多路归并 Compaction 与磁盘 GC 回收等关键路径。

### Changed (变更)
- **SSTableIterator 性能重构**：将 SSTableIterator 内部持有的 `std::shared_ptr` 重构为用完即销毁的原始指针 `SSTableReader*`，实现了零拷贝与轻量化生命周期管理，大幅降低迭代开销。
- **工程规范与代码重构**：将 `CleanupTestDir` 和 `GetFilesByExtension` 等内部辅助函数移入匿名命名空间（`namespace { ... }`），严格遵循内部链接性规范，隐藏实现细节并保持头文件整洁。
- **架构里程碑**：项目正式完成 LSM-Tree 阶段四的核心目标，`KVEngine` 现已具备完整的 LSM-Tree 后台维护能力，支持海量数据的追加写删除、跨层级覆盖与物理空间回收。







# 测试数据


root@LAPTOP-CEH85GLR:/home/requiem/kvstore/build# make && ./kv_benchmark 2>&1 | grep -v -E "\[KVEngine\]|\[SSTableBuilder\]|\[Compaction\]|\[WAL"
[ 53%] Built target kv_server
[100%] Built target kv_benchmark
2026-07-27T16:24:10+08:00
Running ./kv_benchmark
Run on (20 X 2918.4 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x10)
  L1 Instruction 32 KiB (x10)
  L2 Unified 1280 KiB (x10)
  L3 Unified 24576 KiB (x1)
Load Average: 0.36, 0.12, 0.04
-------------------------------------------------------------------------------------------------------------------
Benchmark                                                         Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------------------------
BM_SequentialWrite/128                                         1483 ns         1479 ns       470313 bytes_per_second=95.4381Mi/s items_per_second=676.176k/s
BM_SequentialWrite/1024                                        4013 ns         4013 ns       177543 bytes_per_second=248.107Mi/s items_per_second=249.194k/s
BM_RandomWrite/128                                             1724 ns         1728 ns       426073 bytes_per_second=81.6769Mi/s items_per_second=578.678k/s
BM_RandomWrite/1024                                            4221 ns         4229 ns       166729 bytes_per_second=235.407Mi/s items_per_second=236.438k/s
BM_RandomRead/128                                              1165 ns         1171 ns       615990 bytes_per_second=120.543Mi/s items_per_second=854.047k/s
BM_RandomRead/1024                                             1511 ns         1517 ns       466414 bytes_per_second=656.495Mi/s items_per_second=659.372k/s
KVEngineMTFixture/RandomReadMT/1024/real_time/threads:1        1573 ns         1582 ns       450783 bytes_per_second=632.838Mi/s items_per_second=635.612k/s
KVEngineMTFixture/RandomReadMT/1024/real_time/threads:2        8400 ns         4742 ns        81342 bytes_per_second=237.05Mi/s items_per_second=238.089k/s
KVEngineMTFixture/RandomReadMT/1024/real_time/threads:4       16232 ns         6898 ns        38412 bytes_per_second=245.352Mi/s items_per_second=246.427k/s
KVEngineMTFixture/RandomReadMT/1024/real_time/threads:8       38162 ns        10489 ns        17600 bytes_per_second=208.719Mi/s items_per_second=209.634k/s
KVEngineMTFixture/RandomReadMT/1024/real_time/threads:16      74110 ns        11051 ns         8784 bytes_per_second=214.954Mi/s items_per_second=215.896k/s
root@LAPTOP-CEH85GLR:/home/requiem/kvstore/build# 




加入读写锁之后
root@LAPTOP-CEH85GLR:/home/requiem/kvstore/build# make
[  6%] Building CXX object CMakeFiles/kv_server.dir/src/wal_logger.cpp.o
[ 13%] Linking CXX executable kv_server
[ 53%] Built target kv_server
[ 60%] Building CXX object CMakeFiles/kv_benchmark.dir/src/wal_logger.cpp.o
[ 66%] Linking CXX executable kv_benchmark
[100%] Built target kv_benchmark
root@LAPTOP-CEH85GLR:/home/requiem/kvstore/build# ./kv_benchmark 
2026-07-28T21:57:20+08:00
Running ./kv_benchmark
Run on (20 X 2918.4 MHz CPU s)
CPU Caches:
  L1 Data 48 KiB (x10)
  L1 Instruction 32 KiB (x10)
  L2 Unified 1280 KiB (x10)
  L3 Unified 24576 KiB (x1)
Load Average: 0.14, 0.03, 0.01
-------------------------------------------------------------------------------------------------------------------
Benchmark                                                         Time             CPU   Iterations UserCounters...
-------------------------------------------------------------------------------------------------------------------
BM_SequentialWrite/128                                         1437 ns         1433 ns       508709 bytes_per_second=98.4893Mi/s items_per_second=697.794k/s
BM_SequentialWrite/1024                                        3857 ns         3857 ns       185883 bytes_per_second=258.165Mi/s items_per_second=259.296k/s
BM_RandomWrite/128                                             1628 ns         1634 ns       446131 bytes_per_second=86.3729Mi/s items_per_second=611.95k/s
BM_RandomWrite/1024                                            4269 ns         4277 ns       171363 bytes_per_second=232.813Mi/s items_per_second=233.833k/s
BM_RandomRead/128                                               884 ns          888 ns       800254 bytes_per_second=158.95Mi/s items_per_second=1.12615M/s
BM_RandomRead/1024                                             1075 ns         1080 ns       631510 bytes_per_second=921.961Mi/s items_per_second=926.002k/s
KVEngineMTFixture/RandomReadMT/1024/real_time/threads:1        1099 ns         1109 ns       679244 bytes_per_second=906.028Mi/s items_per_second=909.999k/s
KVEngineMTFixture/RandomReadMT/1024/real_time/threads:2        1204 ns         1209 ns       586954 bytes_per_second=1.61531Gi/s items_per_second=1.66133M/s
KVEngineMTFixture/RandomReadMT/1024/real_time/threads:4        1401 ns         1407 ns       559488 bytes_per_second=2.77693Gi/s items_per_second=2.85604M/s
KVEngineMTFixture/RandomReadMT/1024/real_time/threads:8        1593 ns         1601 ns       439544 bytes_per_second=4.88285Gi/s items_per_second=5.02195M/s
KVEngineMTFixture/RandomReadMT/1024/real_time/threads:16       2871 ns         2883 ns       260464 bytes_per_second=5.41855Gi/s items_per_second=5.57291M/s
root@LAPTOP-CEH85GLR:/home/requiem/kvstore/build# 