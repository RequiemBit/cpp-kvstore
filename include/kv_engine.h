#pragma once

#include "skip_list.h"
#include "wal_logger.h"
#include "sstable_builder.h"
#include "sstable_reader.h"
#include "slice.h"
#include "iterator.h"
#include "types.h"

#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include <shared_mutex>


class KVEngine {
public:
    /**
     * @param db_path 引擎数据根目录
     * @param max_mem_nodes 触发 MemTable 刷盘的节点阈值 (默认 1000)
     * @param max_level 跳表最大层级
     */
    explicit KVEngine(const std::string& db_path, 
                      size_t max_mem_nodes = 20000, 
                      int max_level = 16);
    
    ~KVEngine();

    // 禁用拷贝构造和赋值（存储引擎持有文件资源，不可随意拷贝）
    KVEngine(const KVEngine&) = delete;
    KVEngine& operator=(const KVEngine&) = delete;

    // --- 核心对外 API ---
    void put(const std::string& key, const std::string& value);
    bool get(const std::string& key, std::string& value) const;
    bool erase(const std::string& key);
    
    // 强制刷盘，测试函数，检查是否需要合并（一个阈值）
    void force_flush();
    void debug_print() const;
    bool NeedsCompaction(size_t threshold = 5) const;

    // 多路归并合并磁盘sst文件
    void Compact();

    // 
    std::unique_ptr<Iterator> NewIterator();

    // 刷盘，加载现有的sst，通过wal恢复数据
    void FlushMemTable();
    void LoadExistingSSTables();
    void RecoverAllWals();
private:
    // 用于获取文件路径
    std::string GetWalPath(size_t seq_num) const;
    std::string GetSstPath(size_t seq_num) const;

private:
    // 内存数据结构 (MemTable)
    SkipList<std::string, TableValue> skiplist_;
    
    // 当前活动 WAL 预写日志
    std::string current_wal_path_;
    WalLogger wal_;
    
    // 磁盘 SSTable 句柄列表 (按从新到旧的顺序排列)
    // 这里建议修改为shared_ptr！！！
    std::vector<std::shared_ptr<SSTableReader>> sstables_;
    
    // 状态配置
    std::string db_path_;      // 数据存储目录
    size_t max_mem_nodes_;     // 触发 Flush 的节点数量阈值
    size_t sst_counter_{0};    // SSTable 文件编号计数器
    
    // 做并发优化，区分读写锁
    // mutable std::mutex mtx_;   // 并发保护锁
    mutable std::shared_mutex rw_mutex_;

};
