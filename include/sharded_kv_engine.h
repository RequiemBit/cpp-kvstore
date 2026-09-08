#ifndef SHARDED_KV_ENGINE_H
#define SHARDED_KV_ENGINE_H

#include "kv_engine.h"
#include "spatial_key.h" // 确保能引用到 SpatialKey3D
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <filesystem>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>

struct ShardRange {
    std::string start_key; // 区间起点（包含），若为空表示负无穷
    std::string end_key;   // 区间终点（不包含），若为空表示正无穷
    size_t shard_index;    // 对应的分片索引
};

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

        // 【修改】使用基于 Morton 编码二进制空间的精确均匀切分
        InitUniformRanges();

        // 启动后台统一 Compaction 调度线程
        bg_compact_thread_ = std::thread(&ShardedKVEngine::BackgroundCompactScheduler, this);
    }

    ~ShardedKVEngine() {
        stop_bg_thread_.store(true);
        if (bg_compact_thread_.joinable()) {
            bg_compact_thread_.join();
        }
    }

    ShardedKVEngine(const ShardedKVEngine&) = delete;
    ShardedKVEngine& operator=(const ShardedKVEngine&) = delete;

    void Put(const std::string& key, const std::string& value) {
        GetShard(key).put(key, value);
    }

    bool Get(const std::string& key, std::string* value) {
        if (!value) return false;
        return GetShard(key).get(key, *value);
    }

    bool Get(const std::string& key, std::string& value) {
        return GetShard(key).get(key, value);
    }

    bool Delete(const std::string& key) {
        return GetShard(key).erase(key);
    }

    bool Erase(const std::string& key) {
        return GetShard(key).erase(key);
    }

    size_t GetShardNum() const { return shard_num_; }

    void CompactAll() {
        for (auto& shard : shards_) {
            shard->Compact();
        }
    }

    // 跨分片的精准范围查询接口
    std::vector<IteratorEntry> RangeQuery(const std::string& start_key, const std::string& end_key) {
        std::vector<std::unique_ptr<Iterator>> shard_iterators;

        // 1. 精准相交过滤（Pruning）
        for (const auto& range : ranges_) {
            bool no_overlap = false;
            if (!range.end_key.empty() && range.end_key <= start_key) no_overlap = true;
            if (!range.start_key.empty() && end_key < range.start_key) no_overlap = true;

            if (!no_overlap) {
                shard_iterators.push_back(shards_[range.shard_index]->NewIterator());
            }
        }

        if (shard_iterators.empty()) {
            return {};
        }

        MergingIterator global_merge_iter(std::move(shard_iterators));
        std::vector<IteratorEntry> results;

        global_merge_iter.Seek(start_key);

        while (global_merge_iter.Valid()) {
            auto entry = global_merge_iter.entry();

            if (entry.key > end_key) {
                break;
            }

            if (entry.type != ValueType::kTypeDeletion) {
                results.push_back(entry);
            }

            global_merge_iter.Next();
        }

        return results;
    }
    
private:
    // 【修改】根据 SpatialKey3D 的二进制结构初始化 Morton 空间均分边界
    void InitUniformRanges() {
        ranges_.clear();
        uint64_t step = UINT64_MAX / shard_num_;

        for (size_t i = 0; i < shard_num_; ++i) {
            uint64_t start_code = i * step;
            uint64_t end_code = (i == shard_num_ - 1) ? UINT64_MAX : (i + 1) * step;

            ShardRange r;
            // 构造真实的二进制 SpatialKey 作为分片边界（以图层 0x01 为例）
            SpatialKey3D start_key_struct{0x01, 0, start_code, 0};
            SpatialKey3D end_key_struct{0x01, 0, end_code, 0};

            r.start_key = (i == 0) ? "" : start_key_struct.ToBytes();
            r.end_key = (i == shard_num_ - 1) ? "" : end_key_struct.ToBytes();
            r.shard_index = i;
            ranges_.push_back(r);
        }
    }

    void BackgroundCompactScheduler() {
        while (!stop_bg_thread_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            if (stop_bg_thread_.load()) break;

            for (size_t i = 0; i < shard_num_; ++i) {
                if (stop_bg_thread_.load()) break;
                if (shards_[i]->NeedsCompaction(5)) {
                    shards_[i]->Compact();
                }
            }
        }
    }

    // 【修改】通过二进制区间匹配精准路由到对应的 Shard
    // 【极速优化版】O(1) 纯整型位运算定位分片，彻底消灭字符串比较
    KVEngine& GetShard(const std::string& key) {
        // SpatialKey3D 固定为 18 字节，morton_code 从第 2 字节开始（跳过 layer_type 和 lod_level）
        // 如果传入的 key 长度不对（防御性编程），退化为线性扫描，正常情况下不会触发
        if (key.size() >= sizeof(SpatialKey3D)) {
            // 直接读取大端序的 morton_code 并转回主机序
            const uint64_t* net_morton_ptr = reinterpret_cast<const uint64_t*>(key.data() + 2);
            uint64_t host_morton = be64toh(*net_morton_ptr);
            
            // 因为 InitUniformRanges 是按 UINT64_MAX 平均切分的：
            // shard_num_ 必须是 2 的幂次才能用位移，如果 shard_num_ 不是 2 的幂次，用除法
            // 这里兼容任意 shard_num_：
            uint64_t step = UINT64_MAX / shard_num_;
            size_t shard_index = host_morton / (step + 1); // 防止极端边界溢出
            if (shard_index >= shard_num_) {
                shard_index = shard_num_ - 1;
            }
            return *shards_[shard_index];
        }

        // 兜底逻辑（防止非 SpatialKey 的普通字符串传入）
        for (const auto& range : ranges_) {
            bool ge_start = range.start_key.empty() || key >= range.start_key;
            bool lt_end = range.end_key.empty() || key < range.end_key;
            if (ge_start && lt_end) {
                return *shards_[range.shard_index];
            }
        }
        return *shards_.back();
    }

    size_t shard_num_;
    std::vector<std::unique_ptr<KVEngine>> shards_;
    std::vector<ShardRange> ranges_;

    std::atomic<bool> stop_bg_thread_;
    std::thread bg_compact_thread_;
};

#endif // SHARDED_KV_ENGINE_H