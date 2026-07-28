#ifndef SHARDED_KV_ENGINE_H
#define SHARDED_KV_ENGINE_H

#include "kv_engine.h"
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <filesystem>

class ShardedKVEngine {
public:
    explicit ShardedKVEngine(size_t shard_num = 16, const std::string& base_dir = "./sharded_data")
        : shard_num_(shard_num) {
        
        shards_.reserve(shard_num_);
        for (size_t i = 0; i < shard_num_; ++i) {
            std::string shard_dir = base_dir + "/shard_" + std::to_string(i);
            std::filesystem::create_directories(shard_dir);
            shards_.push_back(std::make_unique<KVEngine>(shard_dir));
        }
    }

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

private:
    KVEngine& GetShard(const std::string& key) {
        size_t hash_val = std::hash<std::string>{}(key);
        return *shards_[hash_val % shard_num_];
    }

    size_t shard_num_;
    std::vector<std::unique_ptr<KVEngine>> shards_;
};

#endif // SHARDED_KV_ENGINE_H