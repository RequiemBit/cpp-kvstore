#pragma once
#include "skip_list.h"
#include <mutex>
#include <string>

template<typename K, typename V>
class KVEngine {
private:
    SkipList<K, V> skiplist;
    mutable std::mutex mtx_; // mutable 允许在 const 函数中加锁

public:
    // 默认构造函数，传递给 SkipList 默认参数
    KVEngine() : skiplist(16) {} 
    
    // 支持自定义最大层级
    explicit KVEngine(int max_level) : skiplist(max_level) {}

    void put(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mtx_);
        skiplist.put(key, value);
    }

    bool get(const K& key, V& value) const {
        std::lock_guard<std::mutex> lock(mtx_);
        return skiplist.get(key, value);
    }

    bool erase(const K& key) {
        std::lock_guard<std::mutex> lock(mtx_);
        return skiplist.erase(key);
    }
    
    // 暴露调试接口
    void debug_print() const {
        std::lock_guard<std::mutex> lock(mtx_);
        skiplist.display();
    }
};