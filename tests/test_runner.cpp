#include "test_runner.h"
#include "kv_engine.h"

#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <cassert>
#include <cassert>
#include <iostream>
#include "skip_list.h"
#include "iterator.h"
#include "spatial_key.h"
#include "voxel_value.h"


namespace fs = std::filesystem;

namespace {

// 辅助函数：清理测试目录
void CleanupTestDir(const std::string& dir) {
    if (fs::exists(dir)) {
        fs::remove_all(dir);
    }
}

// 辅助函数：统计目录下指定后缀的文件列表并排序
std::vector<std::string> GetFilesByExtension(const std::string& dir, const std::string& ext) {
    std::vector<std::string> files;
    if (fs::exists(dir)) {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() == ext) {
                files.push_back(entry.path().filename().string());
            }
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

} // namespace

namespace kvstore::test {

// Test 1: 内存层 Tombstone 单元测试
void TestMemoryTombstone() {
    std::cout << "\n>>> [Test Case 1] Memory Tombstone Test Starting..." << std::endl;
    const std::string test_dir = "./test_tombstone_mem_db";
    CleanupTestDir(test_dir);

    KVEngine engine(test_dir, 100); // threshold 设大，保证留在 MemTable
    std::string val;

    // 1. 写入数据
    engine.put("key_a", "val_a");
    assert(engine.get("key_a", val) && val == "val_a");

    // 2. 删除数据（插入墓碑）
    engine.erase("key_a");

    // 3. 验证 Read Path 是否正确被墓碑挡住
    assert(!engine.get("key_a", val));
    std::cout << "    [PASS] Read path correctly blocked by Memory Tombstone!" << std::endl;

    // 4. 再次写入覆盖墓碑
    engine.put("key_a", "val_a_v2");
    assert(engine.get("key_a", val) && val == "val_a_v2");
    std::cout << "    [PASS] Overwriting Tombstone verified successfully!" << std::endl;
}

// Test 2: SSTable 磁盘层墓碑持久化与跨 SSTable 拦截测试
void TestSSTableDiskTombstone() {
    std::cout << "\n>>> [Test Case 2] SSTable Disk Tombstone & Cross-Layer Blocking Test Starting..." << std::endl;
    const std::string test_dir = "./test_tombstone_disk_db";
    CleanupTestDir(test_dir);

    std::string val;

    {
        KVEngine engine(test_dir, 2); // max_mem_nodes = 2，便于触发 Flush

        // 1. 写入 key1, key2 并触发 Flush -> 生成 000001.sst
        engine.put("key_0001", "val_0001");
        engine.put("key_0002", "val_0002"); // 触发 Flush

        // 2. 删除 key_0001，写入 key_0003 -> 触发 Flush 生成 000002.sst (包含 key_0001 的 Tombstone)
        engine.erase("key_0001");
        engine.put("key_0003", "val_0003"); // 触发 Flush

        // 3. 验证: 最新 SSTable 中的墓碑能否拦截更旧 SSTable 中的真实值
        assert(!engine.get("key_0001", val));
        std::cout << "    [PASS] Cross-SSTable tombstone blocking verified! (000002.sst blocked 000001.sst)" << std::endl;
        
        assert(engine.get("key_0002", val) && val == "val_0002");
        assert(engine.get("key_0003", val) && val == "val_0003");
    }

    // 4. 引擎重启测试：验证从磁盘加载 SSTable 后墓碑是否依然有效
    std::cout << "    [Sub-test] Restarting engine to test disk tombstone persistence..." << std::endl;
    {
        KVEngine engine(test_dir, 2);
        assert(!engine.get("key_0001", val)); // 重启后依然应该返回 false
        assert(engine.get("key_0002", val) && val == "val_0002");
        std::cout << "    [PASS] Disk tombstone persistence across restarts verified successfully!" << std::endl;
    }
}

// Test 3: 集成测试套件与 WAL 轮转测试
void TestFullIntegration() {
    const std::string test_dir = "./test_kv_engine_full_db";
    const size_t max_mem_nodes = 10; 
    const int total_records = 35; 

    CleanupTestDir(test_dir);

    std::cout << "\n==========================================================" << std::endl;
    std::cout << "     KVEngine Full Integration & WAL Rotation Suite      " << std::endl;
    std::cout << "==========================================================\n" << std::endl;

    std::cout << ">>> [Test 3] Initializing Engine & Batch Inserting " << total_records << " records..." << std::endl;
    {
        KVEngine engine(test_dir, max_mem_nodes);

        for (int i = 1; i <= total_records; ++i) {
            char k_buf[32], v_buf[64];
            snprintf(k_buf, sizeof(k_buf), "key_%04d", i);
            snprintf(v_buf, sizeof(v_buf), "value_bytes_payload_%04d", i);
            
            engine.put(k_buf, v_buf);

            if (i % 10 == 0) {
                auto sst_list = GetFilesByExtension(test_dir, ".sst");
                auto wal_list = GetFilesByExtension(test_dir, ".log");
                std::cout << "    [Inserted " << i << " records] Active SSTs: " 
                          << sst_list.size() << " | Active WALs: " << wal_list.size() << std::endl;
            }
        }

        // 1.1 校验自动刷盘产生的 SSTable 数量
        auto sst_files = GetFilesByExtension(test_dir, ".sst");
        std::cout << "\n>>> [Check 1.1] SSTable Generation Result:" << std::endl;
        std::cout << "    Expected SST count: 3 | Actual: " << sst_files.size() << std::endl;
        assert(sst_files.size() == 3);

        // 1.2 校验 WAL 轮转与物理清理功能
        auto wal_files = GetFilesByExtension(test_dir, ".log");
        std::cout << "\n>>> [Check 1.2] WAL Rotation & Cleanup Result:" << std::endl;
        std::cout << "    Expected active WAL count (only current): 1 | Actual: " << wal_files.size() << std::endl;
        assert(wal_files.size() == 1);
        std::cout << "    Current Active WAL file: " << wal_files[0] << std::endl;
        assert(wal_files[0] == "wal_000004.log");

        // 1.3 跨文件层级点查测试
        std::cout << "\n>>> [Check 1.3] Multi-layer Point Query Test:" << std::endl;
        std::string val;
        
        assert(engine.get("key_0005", val) && val == "value_bytes_payload_0005");
        assert(engine.get("key_0015", val) && val == "value_bytes_payload_0015");
        assert(engine.get("key_0025", val) && val == "value_bytes_payload_0025");
        assert(engine.get("key_0033", val) && val == "value_bytes_payload_0033");
        std::cout << "    [PASS] Multi-layer read path verified successfully." << std::endl;

        // 2. Overwrite 优先级校验
        std::cout << "\n>>> [Check 2] Overwrite Priority Test..." << std::endl;
        engine.put("key_0005", "new_updated_payload_0005");
        assert(engine.get("key_0005", val) && val == "new_updated_payload_0005");
        std::cout << "    [PASS] Overwrite priority verified: key_0005 => " << val << std::endl;

        std::cout << "\n>>> Closing engine instance (RAII Close)..." << std::endl;
    } 

    // 3. 崩溃恢复校验
    std::cout << "\n>>> [Test 4] Restarting Engine & Verifying Crash Recovery..." << std::endl;
    {
        KVEngine engine(test_dir, max_mem_nodes);

        std::string val;
        assert(engine.get("key_0002", val) && val == "value_bytes_payload_0002");
        assert(engine.get("key_0018", val) && val == "value_bytes_payload_0018");
        assert(engine.get("key_0028", val) && val == "value_bytes_payload_0028");
        assert(engine.get("key_0033", val) && val == "value_bytes_payload_0033");
        assert(engine.get("key_0005", val) && val == "new_updated_payload_0005");

        std::cout << "    [PASS] Successfully reloaded SSTables and replayed active WAL after restart." << std::endl;
    }
}

// Test 4
void TestCompaction() {
    std::cout << "\n>>> [Test Case 3] SSTable Compaction & Garbage Collection Test..." << std::endl;
    const std::string test_dir = "./test_compaction_db";
    CleanupTestDir(test_dir);

    {
        KVEngine engine(test_dir, 2); // 频繁触发 Flush

        // 1. 写入 key1="v1", key2="v2" -> Flush 到 SSTable 1
        engine.put("key_1", "val_v1");
        engine.put("key_2", "val_v2"); 

        // 2. 覆盖写 key1="v2", 删 key2 -> Flush 到 SSTable 2
        engine.put("key_1", "val_v2_updated");
        engine.erase("key_2");

        // 3. 写入 key3="v3" -> Flush 到 SSTable 3
        engine.put("key_3", "val_v3");
        engine.put("key_4", "val_v4");

        std::string val;
        assert(engine.get("key_1", val) && val == "val_v2_updated");
        assert(!engine.get("key_2", val)); // 被删除

        // 4. 手动执行 Major Compaction
        engine.Compact();

        // 5. 校验 Compaction 后的数据正确性
        assert(engine.get("key_1", val) && val == "val_v2_updated");
        assert(!engine.get("key_2", val)); // 墓碑和旧数据均被抹除，依然返回 false
        assert(engine.get("key_3", val) && val == "val_v3");

        std::cout << "    [PASS] Compaction successfully pruned tombstones and overwritten values!" << std::endl;
    }
}

// 
// 🌟 新增：空间范围查询 / Iterator Seek 专项测试
void TestSpatialRangeQuery() {
    std::cout << "\n==========================================" << std::endl;
    std::cout << "[Test] Running TestSpatialRangeQuery..." << std::endl;
    std::cout << "==========================================" << std::endl;

    std::string test_db_dir = "./test_spatial_db";
    std::filesystem::remove_all(test_db_dir); // 清理旧数据

    {
        KVEngine engine(test_db_dir);

        // 1. 写入批次 1 (旧数据) 并 Flush 到 SSTable 1 (seq = 1)
        engine.put("point_010", "val_10_old");
        engine.put("point_030", "val_30");
        engine.put("point_050", "val_50");
        engine.FlushMemTable();

        // 2. 写入批次 2 (新数据)，更新 point_010 并且新增 point_020 (留在 MemTable，seq = SIZE_MAX)
        engine.put("point_010", "val_10_new"); 
        engine.put("point_020", "val_20");

        // 3. 获取全局合并迭代器
        auto it = engine.NewIterator();

        // ----------------------------------------------------
        // 场景 A: 测试精准 Seek 与同 Key 取新 (Sequence 机制)
        // ----------------------------------------------------
        it->Seek("point_010");
        assert(it->Valid());
        assert(it->entry().key == "point_010");
        // 必须优先吐出 MemTable 里的最新值 val_10_new
        assert(it->entry().value == "val_10_new"); 
        std::cout << "  -> Seek('point_010') passed. Value is latest: " << it->entry().value << std::endl;

        // ----------------------------------------------------
        // 场景 B: 测试 Seek 跳跃到中间不存在的 Key
        // 目标定位到第一个 >= "point_015" 的节点，即 "point_020"
        // ----------------------------------------------------
        it->Seek("point_015");
        assert(it->Valid());
        assert(it->entry().key == "point_020");
        assert(it->entry().value == "val_20");
        std::cout << "  -> Seek('point_015') passed. Found next >= key: " << it->entry().key << std::endl;

        // ----------------------------------------------------
        // 场景 C: 范围遍历测试 (从 point_020 遍历到 point_050)
        // ----------------------------------------------------
        std::vector<std::string> scan_keys;
        while (it->Valid() && it->entry().key <= "point_050") {
            scan_keys.push_back(it->entry().key);
            it->Next();
        }

        // 验证遍历到的顺序：point_020 -> point_030 -> point_050
        assert(scan_keys.size() == 3);
        assert(scan_keys[0] == "point_020");
        assert(scan_keys[1] == "point_030");
        assert(scan_keys[2] == "point_050");
        std::cout << "  -> Range Scan ['point_020' ~ 'point_050'] passed. Total keys: " << scan_keys.size() << std::endl;

        // ----------------------------------------------------
        // 场景 D: Seek 超出范围测试
        // ----------------------------------------------------
        it->Seek("point_099");
        assert(!it->Valid()); // 应该无效
        std::cout << "  -> Seek('point_099') passed. Out of bound correctly." << std::endl;
    }

    std::filesystem::remove_all(test_db_dir); // 清理测试产生的文件
    std::cout << "[PASS] TestSpatialRangeQuery successfully passed!" << std::endl;
}

void RunAllTests() {
    TestMemoryTombstone();
    TestSSTableDiskTombstone();
    TestFullIntegration();
    TestCompaction();
    TestSpatialRangeQuery();
    std::cout << "\n==========================================================" << std::endl;
    std::cout << " 🎉 All Modules & Disk Tombstone Tests PASSED!           " << std::endl;
    std::cout << "==========================================================" << std::endl;
}

} // namespace kvstore::test
