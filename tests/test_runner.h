#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

namespace kvstore::test {

// 运行内存层墓碑测试
void TestMemoryTombstone();

// 运行磁盘层 (SSTable) 墓碑持久化与跨文件拦截测试
void TestSSTableDiskTombstone();

// 运行引擎完整集成测试与 WAL 轮转测试
void TestFullIntegration();

// 运行所有测试套件
void RunAllTests();

} // namespace kvstore::test

#endif // TEST_RUNNER_H