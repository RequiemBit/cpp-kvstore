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
    // 构造函数
    explicit SSTableIterator(SSTableReader* reader, size_t sequence);
    
    // 检查当前的状态
    bool Valid() const override;

    // 检测当前block有没有数据读了，没有就推进到下一个block，没有block就valid_=false直接return，有数据读就加载到current_entry_
    void Next() override;
    // 直接返回current_entry_，当前解析出来的数据
    IteratorEntry entry() const override;

private:
    void ParseCurrentBlock();

    // reader指针，用来读取一个sst文件
    SSTableReader* reader_{nullptr}; // 修改：原始指针
    // 这个sst文件的时间戳
    size_t sequence_;
    
    // 当前读的是第几个data_block
    size_t current_block_idx_{0};
    // 当前data_block的完整字节流
    std::string current_block_data_;
    // 当前data_block内部的游标
    size_t current_offset_{0};
    
    // 是否还有数据需要读取的标志
    bool valid_{false};
    // 当前解析出来的某一条数据
    IteratorEntry current_entry_;
};

// 2. 多路归并迭代器 (MergingIterator)
class MergingIterator : public Iterator {
public:
    // 接收所有的子迭代器。在构造时，它会遍历 children_
    // 让每一个子迭代器调用 Next() 或 entry() 拿到第一条数据，然后把它们全部塞进 min_heap_ 中，完成堆的初始化
    explicit MergingIterator(std::vector<std::unique_ptr<Iterator>> children);

    // 检查归并流是否还有数据
    bool Valid() const override;
    // 核心：推进到下一条全局最小的数据
    void Next() override;
    // 获取当前最小/最新的元素
    IteratorEntry entry() const override;

private:
    struct HeapItem {
        // 这个元素来自哪一个子迭代器（即 children_ 数组的下标）
        size_t child_index;
        // 子迭代器当前正准备吐出的那条真实数据（包含 Key, Value, Type, Sequence）
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

    // 所有参与归并的子迭代器集合（比如 5 个 SSTable，就有 5 个 SSTableIterator）
    std::vector<std::unique_ptr<Iterator>> children_;
    // 优先队列，小顶堆
    std::priority_queue<HeapItem, std::vector<HeapItem>, std::greater<HeapItem>> min_heap_;
    
    // 归并迭代器是否有效
    bool valid_{false};
    // 当前的小顶堆的堆顶元素
    IteratorEntry current_entry_;

    // 检查小顶堆是否为空。如果不为空，就把堆顶元素赋值给 current_entry_；如果为空，就把 valid_ 设为 false
    void FindMin();
};

#endif // KV_ITERATOR_H