#ifndef KV_ITERATOR_H
#define KV_ITERATOR_H

#include "slice.h"
#include "sstable_reader.h"
#include "skip_list.h"
#include "types.h"
#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <algorithm>

// 迭代器输出的条目元素
struct IteratorEntry {
    std::string key;
    std::string value;
    ValueType type{ValueType::kTypeValue};
    size_t sequence{0}; // 序列号：越小说明文件越旧（SSTable ID 越小越旧）
};

// 抽象迭代器基类
class Iterator {
public:
    virtual ~Iterator() = default;
    virtual bool Valid() const = 0;
    virtual void Next() = 0;
    virtual IteratorEntry entry() const = 0;
    // 定位到第一个 key >= target 的位置
    virtual void Seek(const std::string& target) = 0;
};

// SSTable 磁盘迭代器声明
class SSTableIterator : public Iterator {
public:
    SSTableIterator(std::shared_ptr<SSTableReader> reader, size_t sequence);

    bool Valid() const override;
    void Next() override;
    IteratorEntry entry() const override;
    void Seek(const std::string& target) override;

private:
    void ParseCurrentBlock();
    void ParseNextEntry();

    std::shared_ptr<SSTableReader> reader_;
    size_t sequence_{0};
    size_t current_block_idx_{0};
    size_t current_offset_{0};
    std::string current_block_data_;
    IteratorEntry current_entry_;
    bool valid_{false};
};

// 内存快照安全迭代器声明（支持按需传入 lower_bound，避免全表深拷贝触发 bad_alloc）
class SkipListIterator : public Iterator {
public:
    // sequence 传入 SIZE_MAX，确保在多路归并时 MemTable 中的最新数据拥有最高优先级
    // 支持传入 lower_bound，默认从头开始
    explicit SkipListIterator(const SkipList<std::string, TableValue>* list, 
                              size_t sequence = SIZE_MAX, 
                              const std::string& lower_bound = "");

    bool Valid() const override;
    void Next() override;
    IteratorEntry entry() const override;
    // 实现跳转定位
    void Seek(const std::string& target) override;

private:
    std::vector<IteratorEntry> entries_; // 本地快照数据，与底层 SkipList 完全解耦！
    size_t index_{0};                    // 当前遍历的下标
    size_t sequence_{SIZE_MAX};
};

// 多路归并迭代器声明
class MergingIterator : public Iterator {
public:
    explicit MergingIterator(std::vector<std::unique_ptr<Iterator>> children);

    bool Valid() const override;
    void Next() override;
    IteratorEntry entry() const override;
    void Seek(const std::string& target) override;

private:
    struct HeapItem {
        size_t child_index;
        IteratorEntry entry;
        
        bool operator>(const HeapItem& other) const {
            if (entry.key != other.entry.key) {
                return entry.key > other.entry.key;
            }
            return entry.sequence < other.entry.sequence; 
        }
    };

    void FindMin();

    std::vector<std::unique_ptr<Iterator>> children_;
    std::priority_queue<HeapItem, std::vector<HeapItem>, std::greater<HeapItem>> min_heap_;
    bool valid_{false};
    IteratorEntry current_entry_;
};

#endif // KV_ITERATOR_H