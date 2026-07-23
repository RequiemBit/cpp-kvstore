#pragma once
#include "skip_list.h"
#include "wal_logger.h"
#include "sstable_builder.h"
#include "sstable_reader.h"
#include "slice.h"

#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <filesystem>

class KVEngine {
private:
    // 内存数据结构 (MemTable)
    SkipList<std::string, std::string> skiplist_;
    
    // WAL 预写日志
    WalLogger wal_;
    
    // 磁盘 SSTable 句柄列表 (按从新到旧的顺序排列)
    std::vector<std::unique_ptr<SSTableReader>> sstables_;
    
    // 状态配置
    std::string db_path_;      // 数据存储目录
    size_t max_mem_nodes_;     // 触发 Flush 的节点数量阈值
    size_t sst_counter_{0};    // SSTable 文件编号计数器
    
    mutable std::mutex mtx_;   // 并发保护锁

public:
    /**
     * @param db_path 引擎数据根目录
     * @param wal_path WAL 日志路径
     * @param max_mem_nodes 触发 MemTable 刷盘的节点阈值 (默认 1000)
     * @param max_level 跳表最大层级
     */
    explicit KVEngine(const std::string& db_path, 
                     const std::string& wal_path, 
                     size_t max_mem_nodes = 1000, 
                     int max_level = 16) 
        : skiplist_(max_level), 
          wal_(wal_path), 
          db_path_(db_path), 
          max_mem_nodes_(max_mem_nodes) {
        
        std::lock_guard<std::mutex> lock(mtx_);

        // 1. 确保数据目录存在
        if (!std::filesystem::exists(db_path_)) {
            std::filesystem::create_directories(db_path_);
        }

        // 2. 加载磁盘上已存在的 SSTable 文件
        LoadExistingSSTables();

        // 3. 重放 WAL 日志进行崩溃恢复
        auto records = wal_.Recover();
        for (const auto& record : records) {
            if (record.op == OperationType::PUT) {
                skiplist_.put(record.key, record.value);
            } else if (record.op == OperationType::ERASE) {
                skiplist_.erase(record.key);
            }
        }
        std::cout << "[KVEngine] Recovery complete. Engine ready." << std::endl;
    }

    // --- 写操作 ---
    void put(const std::string& key, const std::string& value) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        // 1. 先落 WAL 日志 (使用 Slice 实现零拷贝写入)
        wal_.Append(OperationType::PUT, key, value);

        // 2. 更新内存 MemTable
        skiplist_.put(key, value);

        // 3. 容量检测：超限则触发 Flush 刷盘
        if (skiplist_.size() >= max_mem_nodes_) {
            FlushMemTable();
        }
    }

    // --- 读操作 (多层检索链路) ---
    bool get(const std::string& key, std::string& value) const {
        std::lock_guard<std::mutex> lock(mtx_);

        // 1. 第一优先级：检索 MemTable (内存)
        if (skiplist_.get(key, value)) {
            return true;
        }

        // 2. 第二优先级：检索 SSTables (磁盘，从最新到最旧)
        for (const auto& reader : sstables_) {
            if (reader->Get(key, &value)) {
                return true;
            }
        }

        return false;
    }

    // --- 删除操作 ---
    bool erase(const std::string& key) {
        std::lock_guard<std::mutex> lock(mtx_);

        // 1. 写 WAL
        wal_.Append(OperationType::ERASE, key, "");

        // 2. 从内存删除 (注意：对于 LSM-Tree，生产环境通常写 Tombstone 墓碑值，这里先做内存抹除)
        bool removed = skiplist_.erase(key);

        return removed;
    }

    // 手动触发刷盘接口
    void force_flush() {
        std::lock_guard<std::mutex> lock(mtx_);
        FlushMemTable();
    }

    // 调试打印内存 SkipList 结构
    void debug_print() const {
        std::lock_guard<std::mutex> lock(mtx_);
        skiplist_.display();
    }

private:
    // 将当前 MemTable 的内容写入 SSTable
    void FlushMemTable() {
        if (skiplist_.size() == 0) return;

        // 生成全新的 SSTable 文件名 (如 "db_data/000001.sst")
        char filename_buf[64];
        snprintf(filename_buf, sizeof(filename_buf), "%06zu.sst", ++sst_counter_);
        std::string sst_path = (std::filesystem::path(db_path_) / filename_buf).string();

        std::cout << "[KVEngine] MemTable limit reached. Flushing to " << sst_path << "..." << std::endl;

        // 1. 初始化 SSTableBuilder
        SSTableBuilder builder(sst_path);

        // 2. 顺序遍历 SkipList（SkipList 本身就是有序的，完美契合 SSTable 要求）
        // 假设 SkipList 提供了 Iterator 接口；若无迭代器，可使用 Dump/Vector 导出一份有序节点列表
        auto all_kv = skiplist_.dump_all(); 
        for (const auto& [k, v] : all_kv) {
            builder.Add(k, v);
        }
        builder.Finish();

        // 3. 打开新建的 SSTable 并插入到 sstables_ 头部 (最新的 SSTable 优先被检索)
        auto reader = SSTableReader::Open(sst_path);
        if (reader) {
            sstables_.insert(sstables_.begin(), std::move(reader));
        }

        // 4. 清空内存 SkipList
        skiplist_.clear();

        std::cout << "[KVEngine] Flush complete. SSTables active count: " << sstables_.size() << std::endl;
    }

    // 启动时加载已有的 `.sst` 文件
    void LoadExistingSSTables() {
        std::vector<std::string> sst_files;
        for (const auto& entry : std::filesystem::directory_iterator(db_path_)) {
            if (entry.path().extension() == ".sst") {
                sst_files.push_back(entry.path().string());
            }
        }

        // 按文件名正序排序
        std::sort(sst_files.begin(), sst_files.end());

        // 文件名越大的越新，所以倒序插入（保持最新的 SSTable 在 sstables_ 头部）
        for (auto it = sst_files.rbegin(); it != sst_files.rend(); ++it) {
            auto reader = SSTableReader::Open(*it);
            if (reader) {
                sstables_.push_back(std::move(reader));
            }
        }
        
        sst_counter_ = sst_files.size();
    }
};
