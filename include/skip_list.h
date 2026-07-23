#pragma once
#include <iostream>
#include <vector>
#include <random>
#include <functional>
#include <utility> // for std::pair

// 跳表的节点
template<typename K, typename V>
struct SkipListNode {
    K key;
    V value;
    // 存储在第i层的大于value的节点的指针
    std::vector<SkipListNode*> forward; 

    SkipListNode(const K& k, const V& v, int level)
        : key(k), value(v), forward(level, nullptr) {}
};

template<typename K, typename V>
class SkipList {
private:
    SkipListNode<K, V>* head;
    // 最大高度 
    int max_level;
    // 当前层数            
    int current_level;        
    // 节点总数记录（新增：用于 O(1) 获取 size）
    size_t node_count_;
    
    // 静态随机数引擎
    static std::mt19937 gen;
    static std::uniform_real_distribution<> dis;

    int random_level() {
        int lvl = 1;
        while (dis(gen) < 0.5 && lvl < max_level) {
            lvl++;
        }
        return lvl;
    }

public:
    explicit SkipList(int max_lvl = 16) 
        : max_level(max_lvl), current_level(1), node_count_(0) {
        // 构造一个空的头节点，不存真实数据
        head = new SkipListNode<K, V>(K(), V(), max_level);
    }

    ~SkipList() {
        clear();
        delete head;
    }

    // --- 新增 1：获取节点总数 ---
    size_t size() const {
        return node_count_;
    }

    // --- 新增 2：清空所有节点（Flush 刷盘后重置 MemTable）---
    void clear() {
        SkipListNode<K, V>* curr = head->forward[0];
        while (curr != nullptr) {
            SkipListNode<K, V>* next = curr->forward[0];
            delete curr;
            curr = next;
        }
        // 重置头节点指向与层级
        for (int i = 0; i < max_level; ++i) {
            head->forward[i] = nullptr;
        }
        current_level = 1;
        node_count_ = 0;
    }

    // --- 新增 3：导出所有 KV 对（按 Key 升序，用于 Flush 生成 SSTable）---
    std::vector<std::pair<K, V>> dump_all() const {
        std::vector<std::pair<K, V>> result;
        result.reserve(node_count_);
        
        // 第 0 层包含所有的节点且天然有序
        SkipListNode<K, V>* curr = head->forward[0];
        while (curr != nullptr) {
            result.emplace_back(curr->key, curr->value);
            curr = curr->forward[0];
        }
        return result;
    }

    // 插入节点
    void put(const K& key, const V& value) {
        std::vector<SkipListNode<K, V>*> update(max_level, nullptr);
        SkipListNode<K, V>* curr = head;

        for (int i = current_level - 1; i >= 0; --i) {
            while (curr->forward[i] != nullptr && curr->forward[i]->key < key) {
                curr = curr->forward[i];
            }
            update[i] = curr;
        }

        curr = curr->forward[0];

        // Key 存在，更新 Value
        if (curr != nullptr && curr->key == key) {
            curr->value = value;
            return;
        }

        // Key 不存在，插入新节点
        int new_level = random_level();
        if (new_level > current_level) {
            for (int i = current_level; i < new_level; ++i) {
                update[i] = head;
            }
            current_level = new_level;
        }

        SkipListNode<K, V>* new_node = new SkipListNode<K, V>(key, value, new_level);
        for (int i = 0; i < new_level; ++i) {
            new_node->forward[i] = update[i]->forward[i];
            update[i]->forward[i] = new_node;
        }

        // 维护节点总数
        node_count_++;
    }

    // 查
    bool get(const K& key, V& out_value) const {
        SkipListNode<K, V>* curr = head;
        for (int i = current_level - 1; i >= 0; --i) {
            while (curr->forward[i] != nullptr && curr->forward[i]->key < key) {
                curr = curr->forward[i];
            }
        }
        curr = curr->forward[0];
        if (curr != nullptr && curr->key == key) {
            out_value = curr->value;
            return true;
        }
        return false;
    }

    // 删除节点
    bool erase(const K& key) {
        std::vector<SkipListNode<K, V>*> update(max_level, nullptr);
        SkipListNode<K, V>* curr = head;

        for (int i = current_level - 1; i >= 0; --i) {
            while (curr->forward[i] != nullptr && curr->forward[i]->key < key) {
                curr = curr->forward[i];
            }
            update[i] = curr;
        }

        curr = curr->forward[0];

        if (curr != nullptr && curr->key == key) {
            for (size_t i = 0; i < curr->forward.size(); ++i) {
                if (update[i]->forward[i] == curr) {
                    update[i]->forward[i] = curr->forward[i];
                }
            }
            delete curr;

            while (current_level > 1 && head->forward[current_level - 1] == nullptr) {
                current_level--;
            }

            // 维护节点总数
            node_count_--;
            return true;
        }
        return false;
    }
    
    // 打印跳表结构（调试用）
    void display() const {
        std::cout << "\n=== SkipList Structure (Top-Down) ===" << std::endl;
        
        for (int i = current_level - 1; i >= 0; --i) {
            std::cout << "Level " << i << ": HEAD";
            
            SkipListNode<K, V>* curr = head->forward[i];
            while (curr != nullptr) {
                std::cout << " -> [" << curr->key << ":" << curr->value << "]";
                curr = curr->forward[i];
            }
            std::cout << " -> NULL" << std::endl;
        }
        std::cout << "=====================================\n" << std::endl;
    }
};

// 静态成员变量类外定义
template<typename K, typename V>
std::mt19937 SkipList<K, V>::gen(std::random_device{}());

template<typename K, typename V>
std::uniform_real_distribution<> SkipList<K, V>::dis(0.0, 1.0);