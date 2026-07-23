#include "sstable_builder.h"
#include "sstable_reader.h"
#include <iostream>
#include <cassert>

int main() {
    std::string sst_filename = "test_data.sst";

    // 1. 生成 SSTable 文件 (128 字节一个 block，生成多块索引)
    std::cout << "=== Phase 1: Building SSTable ===" << std::endl;
    {
        SSTableBuilder builder(sst_filename, 128);
        
        // 必须按字典序写入！
        // 比如："key_00", "key_01", ... "key_99"
        for (int i = 0; i < 100; ++i) {
            char k_buf[16], v_buf[32];
            snprintf(k_buf, sizeof(k_buf), "key_%02d", i);
            snprintf(v_buf, sizeof(v_buf), "val_payload_%02d", i);
            builder.Add(k_buf, v_buf);
        }
        builder.Finish();
    }

    // 2. 使用 SSTableReader 读取验证
    std::cout << "\n=== Phase 2: Reading & Searching SSTable ===" << std::endl;
    {
        auto reader = SSTableReader::Open(sst_filename);
        assert(reader != nullptr);

        std::string val;

        // 验证查存在的 Key
        if (reader->Get("key_42", &val)) {
            std::cout << "[Success] Found key_42 => " << val << std::endl;
            assert(val == "val_payload_42");
        } else {
            std::cerr << "[Error] Key key_42 not found!" << std::endl;
        }

        if (reader->Get("key_00", &val)) {
            std::cout << "[Success] Found key_00 => " << val << std::endl;
            assert(val == "val_payload_00");
        }

        if (reader->Get("key_99", &val)) {
            std::cout << "[Success] Found key_99 => " << val << std::endl;
            assert(val == "val_payload_99");
        }

        // 验证查不存在的 Key
        bool found_absent = reader->Get("key_100", &val);
        std::cout << "[Check] Searching for non-existent key_100: " 
                  << (found_absent ? "FOUND (Bug)" : "NOT FOUND (Correct!)") << std::endl;
        assert(!found_absent);
    }

    std::cout << "\n🎉 SSTable 读写模块（阶段三二步）全面测试通过！" << std::endl;
    return 0;
}