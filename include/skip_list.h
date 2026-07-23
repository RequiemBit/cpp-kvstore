#pragma once
#include <iostream>
#include <vector>
#include <random>
#include <functional> // for std::hash (optional)

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
    
    // 优化：静态随机数引擎，避免每次创建对象都初始化庞大的 mt19937 状态
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
        : max_level(max_lvl), current_level(1) {
        // 构造一个空的头节点，不存真实数据
        head = new SkipListNode<K, V>(K(), V(), max_level);
    }

    ~SkipList() {
        SkipListNode<K, V>* curr = head;
        while (curr != nullptr) {
            SkipListNode<K, V>* next = curr->forward[0];
            delete curr;
            curr = next;
        }
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
            return true;
        }
        return false;
    }
    
    // // 辅助函数：打印跳表结构（调试用）
    // void display() const {
    //     std::cout << "=== SkipList Content ===" << std::endl;
    //     for (int i = 0; i < current_level; ++i) {
    //         std::cout << "Level " << i << ": ";
    //         SkipListNode<K, V>* node = head->forward[i];
    //         while (node != nullptr) {
    //             std::cout << "[" << node->key << ":" << node->value << "] -> ";
    //             node = node->forward[i];
    //         }
    //         std::cout << "NULL" << std::endl;
    //     }
    //     std::cout << "========================" << std::endl;
    // }

    // 在 SkipList 类的 public 部分添加此方法
    void display() const {
        std::cout << "\n=== SkipList Structure (Top-Down) ===" << std::endl;
        
        // 从最高层向下遍历
        for (int i = current_level - 1; i >= 0; --i) {
            std::cout << "Level " << i << ": HEAD";
            
            SkipListNode<K, V>* curr = head->forward[i];
            while (curr != nullptr) {
                // 格式化输出: [key:value]
                std::cout << " -> [" << curr->key << ":" << curr->value << "]";
                curr = curr->forward[i];
            }
            std::cout << " -> NULL" << std::endl;
        }
        std::cout << "=====================================\n" << std::endl;
    }
  
};

// 静态成员变量必须在类外定义
template<typename K, typename V>
std::mt19937 SkipList<K, V>::gen(std::random_device{}());

template<typename K, typename V>
std::uniform_real_distribution<> SkipList<K, V>::dis(0.0, 1.0);