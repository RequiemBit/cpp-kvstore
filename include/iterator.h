#ifndef KV_ITERATOR_H
#define KV_ITERATOR_H

#include "slice.h"
#include "sstable_reader.h"
#include <memory>
#include <string>
#include <vector>
#include <queue>

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
};

// 1. 单个 SSTable 遍历迭代器
class SSTableIterator : public Iterator {
public:
    explicit SSTableIterator(SSTableReader* reader, size_t sequence);
    
    bool Valid() const override;
    void Next() override;
    IteratorEntry entry() const override;

private:
    void ParseCurrentBlock();

    SSTableReader* reader_{nullptr}; // 修改：原始指针
    size_t sequence_;
    
    size_t current_block_idx_{0};
    std::string current_block_data_;
    size_t current_offset_{0};
    
    bool valid_{false};
    IteratorEntry current_entry_;
};

// 2. 多路归并迭代器 (MergingIterator)
class MergingIterator : public Iterator {
public:
    explicit MergingIterator(std::vector<std::unique_ptr<Iterator>> children);

    bool Valid() const override;
    void Next() override;
    IteratorEntry entry() const override;

private:
    struct HeapItem {
        size_t child_index;
        IteratorEntry entry;
        
        // operator> 用于 std::greater 构造 Min-Heap（小顶堆）
        bool operator>(const HeapItem& other) const {
            if (entry.key != other.entry.key) {
                return entry.key > other.entry.key; // Key 字典序小的排前面
            }
            // 同 Key 时，sequence 较大的（更新的数据）应该先被吐出来
            // 在 std::greater 下，operator> 为 true 代表 priority 更低（排在后面）
            return entry.sequence < other.entry.sequence; 
        }
    };

    std::vector<std::unique_ptr<Iterator>> children_;
    std::priority_queue<HeapItem, std::vector<HeapItem>, std::greater<HeapItem>> min_heap_;
    bool valid_{false};
    IteratorEntry current_entry_;

    void FindMin();
};

#endif // KV_ITERATOR_H