#ifndef SHARDED_KV_ENGINE_H
#define SHARDED_KV_ENGINE_H

#include "kv_engine.h"
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <filesystem>
#include <thread>
#include <atomic>
#include <chrono>

class ShardedKVEngine {
public:
    explicit ShardedKVEngine(size_t shard_num = 16, const std::string& base_dir = "./sharded_data")
        : shard_num_(shard_num), stop_bg_thread_(false) {
        
        shards_.reserve(shard_num_);
        for (size_t i = 0; i < shard_num_; ++i) {
            std::string shard_dir = base_dir + "/shard_" + std::to_string(i);
            std::filesystem::create_directories(shard_dir);
            shards_.push_back(std::make_unique<KVEngine>(shard_dir));
        }

        // 启动后台统一 Compaction 调度线程
        bg_compact_thread_ = std::thread(&ShardedKVEngine::BackgroundCompactScheduler, this);
    }

    ~ShardedKVEngine() {
        // 优雅通知后台线程退出并等待安全回收
        stop_bg_thread_.store(true);
        if (bg_compact_thread_.joinable()) {
            bg_compact_thread_.join();
        }
    }

    // 禁用拷贝构造与赋值（持有利有线程与文件资源）
    ShardedKVEngine(const ShardedKVEngine&) = delete;
    ShardedKVEngine& operator=(const ShardedKVEngine&) = delete;

    // 1. Put：对接小引擎的 put
    void Put(const std::string& key, const std::string& value) {
        GetShard(key).put(key, value);
    }

    // 2. Get：对接小引擎的 get（指针版本）
    bool Get(const std::string& key, std::string* value) {
        if (!value) return false;
        return GetShard(key).get(key, *value);
    }

    // 重载 Get（引用版本）
    bool Get(const std::string& key, std::string& value) {
        return GetShard(key).get(key, value);
    }

    // 3. Delete / Erase：精确对接小引擎的 erase
    bool Delete(const std::string& key) {
        return GetShard(key).erase(key);
    }

    bool Erase(const std::string& key) {
        return GetShard(key).erase(key);
    }

    size_t GetShardNum() const { return shard_num_; }

    // 手动触发所有 Shard 合并（供测试或特定维护逻辑调用）
    void CompactAll() {
        for (auto& shard : shards_) {
            shard->Compact();
        }
    }

private:
    // 后台统一调度器：单线程串行轮询，防止多 Shard 同时 Compact 产生磁盘 IO 风暴
    void BackgroundCompactScheduler() {
        while (!stop_bg_thread_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (stop_bg_thread_.load()) break;

            for (size_t i = 0; i < shard_num_; ++i) {
                if (stop_bg_thread_.load()) break;

                // 当单个 Shard 内 SSTable 文件达到 5 个时自动触发后台归并
                if (shards_[i]->NeedsCompaction(5)) {
                    shards_[i]->Compact();
                }
            }
        }
    }

    KVEngine& GetShard(const std::string& key) {
        size_t hash_val = std::hash<std::string>{}(key);
        return *shards_[hash_val % shard_num_];
    }

    size_t shard_num_;
    std::vector<std::unique_ptr<KVEngine>> shards_;

    // 后台 Compaction 线程控制变量
    std::atomic<bool> stop_bg_thread_;
    std::thread bg_compact_thread_;
};

#endif // SHARDED_KV_ENGINE_H