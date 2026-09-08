#include "kv_engine.h"
#include <iostream>
#include <map>
#include <string>
#include <cassert>
#include <random>
#include <filesystem>
#include <vector>

// 清理测试目录
void CleanupDb(const std::string& path) {
    if (std::filesystem::exists(path)) {
        std::filesystem::remove_all(path);
    }
}

// 核心断言：对比 KVEngine 的范围查询结果与 std::map 是否逐字对应
void VerifyRangeQuery(KVEngine& engine, const std::map<std::string, std::string>& oracle, 
                      const std::string& start_key, const std::string& end_key) {
    
    // 1. 获取 std::map 中符合 [start_key, end_key) 的期望结果
    std::vector<std::pair<std::string, std::string>> expected;
    auto it_map = oracle.lower_bound(start_key);
    while (it_map != oracle.end() && it_map->first < end_key) {
        expected.emplace_back(it_map->first, it_map->second);
        ++it_map;
    }

    // 2. 获取 KVEngine 迭代器的实际结果 (适配你当前的 iterator entry() 接口)
    std::vector<std::pair<std::string, std::string>> actual;
    auto iter = engine.NewIterator();
    iter->Seek(start_key);
    while (iter->Valid()) {
        auto entry = iter->entry();
        std::string k = entry.key;
        if (k >= end_key) {
            break; // 超出上限
        }
        actual.emplace_back(k, entry.value);
        iter->Next();
    }

    // 3. 严格比对
    if (expected.size() != actual.size()) {
        std::cerr << "[FAIL] Range query size mismatch! Range: [" << start_key << ", " << end_key << ")"
                  << " Expected size: " << expected.size() << ", Actual size: " << actual.size() << std::endl;
        assert(false);
    }

    for (size_t i = 0; i < expected.size(); ++i) {
        if (expected[i].first != actual[i].first || expected[i].second != actual[i].second) {
            std::cerr << "[FAIL] Data mismatch at index " << i << "!"
                      << " Expected: [" << expected[i].first << ":" << expected[i].second << "]"
                      << " Actual: [" << actual[i].first << ":" << actual[i].second << "]" << std::endl;
            assert(false);
        }
    }
}

int main() {
    std::string db_path = "./test_range_db_correctness";
    CleanupDb(db_path);

    {
        // 设较小的 max_mem_nodes=50，逼迫系统频繁触发 Flush，
        // 从而强力测试 MemTable + 多个 SSTable 的多路归并范围查询正确性！
        KVEngine engine(db_path, 50, 16);
        std::map<std::string, std::string> oracle;

        std::cout << "[Test] Phase 1: Sequential & Random Insertion with Frequent Flushes..." << std::endl;
        
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(1, 1000);

        // 写入 500 个 KV 对，制造多轮 MemTable 刷盘
        for (int i = 0; i < 500; ++i) {
            std::string k = "key_" + std::to_string(dist(rng) * 10 + (i % 7)); // 制造一些重复和交错的key
            std::string v = "val_" + std::to_string(i);
            
            engine.put(k, v);
            oracle[k] = v;

            // 每隔一定步数强行触发一次手动刷盘，测试混合状态
            if (i % 120 == 0) {
                engine.force_flush();
            }
        }

        std::cout << "[Test] Phase 2: Running Comprehensive Range Queries..." << std::endl;

        // 测试各种边界范围查询
        std::vector<std::pair<std::string, std::string>> test_ranges = {
            {"key_000", "key_9999"}, // 全区间
            {"key_200", "key_500"},  // 中间子区间
            {"key_999", "key_Z"},    // 超出上限的区间
            {"key_0",   "key_100"},  // 偏左区间
            {"key_500", "key_500"},  // 空区间 (start == end)
            {"non_existent_start", "zzzz"} // 不存在的起始键
        };

        for (const auto& range : test_ranges) {
            VerifyRangeQuery(engine, oracle, range.first, range.second);
        }
        std::cout << "[PASS] All dynamic range query checks passed successfully!" << std::endl;
    }

    CleanupDb(db_path);
    std::cout << "🎉 范围查询正确性测试全部通过！" << std::endl;
    return 0;
}