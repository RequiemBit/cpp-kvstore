#pragma once
#include "skip_list.h"
#include "wal_logger.h"
#include <mutex>
#include <string>
#include <sstream>
#include <iostream>

template<typename K = std::string, typename V = std::string>
class KVEngine {
private:
    SkipList<K, V> skiplist;
    WalLogger wal_;
    mutable std::mutex mtx_; // mutable 允许在 const 函数中加锁

    // 内部辅助函数：通用类型转 std::string（用于 WAL 序列化）
    template<typename T>
    std::string to_string_internal(const T& val) const {
        if constexpr (std::is_same_v<T, std::string>) {
            return val;
        } else {
            std::ostringstream oss;
            oss << val;
            return oss.str();
        }
    }

    // 内部辅助函数：std::string 转通用类型（用于崩溃恢复反序列化）
    template<typename T>
    T from_string_internal(const std::string& str) const {
        if constexpr (std::is_same_v<T, std::string>) {
            return str;
        } else {
            std::istringstream iss(str);
            T val;
            iss >> val;
            return val;
        }
    }

public:
    // 构造函数：必须指定 wal 日志路径
    explicit KVEngine(const std::string& wal_path, int max_level = 16) 
        : skiplist(max_level), wal_(wal_path) {
        
        // 1. 系统启动时自动进行崩溃恢复 (Recovery)
        std::lock_guard<std::mutex> lock(mtx_);
        auto records = wal_.Recover();
        
        for (const auto& record : records) {
            K key = from_string_internal<K>(record.key);
            if (record.op == OperationType::PUT) {
                V val = from_string_internal<V>(record.value);
                skiplist.put(key, val);
            } else if (record.op == OperationType::ERASE) {
                skiplist.erase(key);
            }
        }
        std::cout << "[KVEngine] Recovery complete. Engine ready." << std::endl;
    }

    // 写操作：先落 WAL 日志，再更新内存
    void put(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(mtx_);
        
        // 1. 写预写日志 (WAL)
        std::string k_str = to_string_internal(key);
        std::string v_str = to_string_internal(value);
        wal_.Append(OperationType::PUT, k_str, v_str);

        // 2. 更新内存数据结构
        skiplist.put(key, value);
    }

    // 读操作：只读内存 (跳表)
    bool get(const K& key, V& value) const {
        std::lock_guard<std::mutex> lock(mtx_);
        return skiplist.get(key, value);
    }

    // 删除操作：先落 WAL 日志，再从内存删除
    bool erase(const K& key) {
        std::lock_guard<std::mutex> lock(mtx_);

        // 1. 写预写日志 (WAL)
        std::string k_str = to_string_internal(key);
        wal_.Append(OperationType::ERASE, k_str, "");

        // 2. 删除内存节点
        return skiplist.erase(key);
    }
    
    // 调试接口
    void debug_print() const {
        std::lock_guard<std::mutex> lock(mtx_);
        skiplist.display();
    }
};
