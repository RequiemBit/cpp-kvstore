#include "kv_engine.h"
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <algorithm>
#include <cassert>

namespace fs = std::filesystem;

// 辅助函数：清理测试目录
void CleanupTestDir(const std::string& dir) {
    if (fs::exists(dir)) {
        fs::remove_all(dir);
    }
}

// 辅助函数：统计目录下后缀为 .sst 的文件数量并返回文件名列表
std::vector<std::string> GetSSTFiles(const std::string& dir) {
    std::vector<std::string> sst_files;
    if (fs::exists(dir)) {
        for (const auto& entry : fs::directory_iterator(dir)) {
            if (entry.path().extension() == ".sst") {
                sst_files.push_back(entry.path().filename().string());
            }
        }
    }
    std::sort(sst_files.begin(), sst_files.end());
    return sst_files;
}

int main() {
    const std::string test_dir = "./test_multi_sst_db";
    const std::string wal_path = test_dir + "/wal.log";
    
    // 每 10 条数据触发一次 Flush 刷盘
    const size_t max_mem_nodes = 10; 
    const int total_records = 35; // 预期触发 3 次自动 Flush（生成 000001.sst, 000002.sst, 000003.sst）

    CleanupTestDir(test_dir);

    std::cout << "===========================================" << std::endl;
    std::cout << "     Multi-SST Generation Test Suite       " << std::endl;
    std::cout << "===========================================\n" << std::endl;

    std::cout << ">>> Initializing KVEngine with max_mem_nodes = " << max_mem_nodes << "..." << std::endl;
    {
        KVEngine engine(test_dir, wal_path, max_mem_nodes);

        std::cout << ">>> Inserting " << total_records << " records to trigger multiple Flushes..." << std::endl;
        for (int i = 1; i <= total_records; ++i) {
            char k_buf[32], v_buf[64];
            snprintf(k_buf, sizeof(k_buf), "key_%04d", i);
            snprintf(v_buf, sizeof(v_buf), "value_bytes_payload_%04d", i);
            
            engine.put(k_buf, v_buf);

            // 当插入第 10, 20, 30 条数据时，查看产生的 SST 文件数
            if (i % 10 == 0) {
                auto sst_list = GetSSTFiles(test_dir);
                std::cout << "    [Inserted " << i << " records] Current SST files count on disk: " 
                          << sst_list.size() << std::endl;
            }
        }

        // --- 1. 自动刷盘数量断言校验 ---
        auto files_before_close = GetSSTFiles(test_dir);
        std::cout << "\n>>> [Check 1] Automatic Flush Result:" << std::endl;
        std::cout << "    Expected automatically flushed files: 3" << std::endl;
        std::cout << "    Actual SST files on disk: " << files_before_close.size() << std::endl;
        assert(files_before_close.size() == 3);
        
        for (const auto& file : files_before_close) {
            std::cout << "    - Found file: " << file << std::endl;
        }

        // --- 2. 检索各个 SSTable 中的数据 ---
        std::cout << "\n>>> [Check 2] Querying records from different SSTables..." << std::endl;
        std::string val;
        
        // 来自 第 1 个 SSTable (000001.sst: key 1~10)
        assert(engine.get("key_0005", val) && val == "value_bytes_payload_0005");
        std::cout << "    [PASS] Read key_0005 from SSTable 1." << std::endl;

        // 来自 第 2 个 SSTable (000002.sst: key 11~20)
        assert(engine.get("key_0015", val) && val == "value_bytes_payload_0015");
        std::cout << "    [PASS] Read key_0015 from SSTable 2." << std::endl;

        // 来自 第 3 个 SSTable (000003.sst: key 21~30)
        assert(engine.get("key_0025", val) && val == "value_bytes_payload_0025");
        std::cout << "    [PASS] Read key_0025 from SSTable 3." << std::endl;

        // 来自 内存 SkipList (还没刷盘的 key 31~35)
        assert(engine.get("key_0033", val) && val == "value_bytes_payload_0033");
        std::cout << "    [PASS] Read key_0033 from MemTable." << std::endl;

        std::cout << "\n>>> Closing engine instance..." << std::endl;
    } // 析构对象

    // --- 3. 验证重启后的加载能力 ---
    std::cout << "\n>>> [Check 3] Restarting Engine & Reloading all SSTables..." << std::endl;
    {
        KVEngine engine(test_dir, wal_path, max_mem_nodes);

        // 重新检查所有数据是否能在多个 SSTable 中无缝跨文件查询
        std::string val;
        assert(engine.get("key_0002", val) && val == "value_bytes_payload_0002");
        assert(engine.get("key_0018", val) && val == "value_bytes_payload_0018");
        assert(engine.get("key_0028", val) && val == "value_bytes_payload_0028");

        std::cout << "    [PASS] Successfully reloaded and queried across all SSTable files after restart." << std::endl;
    }

    std::cout << "\n===========================================" << std::endl;
    std::cout << " 🎉 Multi-SST Generation Test PASSED!     " << std::endl;
    std::cout << "===========================================" << std::endl;

    return 0;
}