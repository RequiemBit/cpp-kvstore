#include "iterator.h"
#include <cstring>
#include <algorithm>
#include <iostream>

static uint32_t DecodeFixed32(const char* ptr) {
    uint32_t result;
    std::memcpy(&result, ptr, sizeof(result));
    return result;
}

// -------------------------------------------------------------------
// SSTableIterator 实现
// -------------------------------------------------------------------
SSTableIterator::SSTableIterator(std::shared_ptr<SSTableReader> reader, size_t sequence)
    : reader_(reader), sequence_(sequence) {
    if (reader_ && reader_->GetBlockCount() > 0) {
        current_block_idx_ = 0;
        current_offset_ = 0;
        ParseCurrentBlock();
    } else {
        valid_ = false;
    }
}

// 构造时调用，读取第一个块的第一条数据，while是防御性编程，防止block损坏了
void SSTableIterator::ParseCurrentBlock() {
    valid_ = false;
    while (current_block_idx_ < reader_->GetBlockCount()) {
        current_block_data_ = reader_->ReadDataBlock(current_block_idx_);
        current_offset_ = 0;
        if (!current_block_data_.empty()) {
            // 直接解析 offset = 0 处的第一条记录
            ParseNextEntry();
            if (valid_) {
                return;
            }
        }
        current_block_idx_++;
    }
}

// 是否还有数据读
bool SSTableIterator::Valid() const {
    return valid_;
}

// 检测当前block有没有数据读了，没有就推进到下一个block，没有block就valid_=false直接return，有数据读就加载到current_entry_
void SSTableIterator::Next() {
    if (!valid_ || !reader_) {
        valid_ = false;
        return;
    }
    // 直接尝试解析下一条记录
    ParseNextEntry();
}

void SSTableIterator::ParseNextEntry() {
    // 1. 如果当前 Block 读完了，跨 Block 寻找下一个有数据的 Block
    while (current_offset_ >= current_block_data_.size()) {
        current_block_idx_++;
        if (current_block_idx_ >= reader_->GetBlockCount()) {
            valid_ = false; // 彻底没有 Block 了，迭代器失效
            return;
        }
        current_block_data_ = reader_->ReadDataBlock(current_block_idx_);
        current_offset_ = 0;
    }

    // 2. 解析当前 current_offset_ 位置的 entry
    const char* p = current_block_data_.data() + current_offset_;
    const char* limit = current_block_data_.data() + current_block_data_.size();

    // Header 校验: type (1B) + klen (4B) + vlen (4B) = 9B
    if (p + 9 > limit) {
        valid_ = false;
        return;
    }

    uint8_t type_byte = static_cast<uint8_t>(p[0]);
    uint32_t klen = DecodeFixed32(p + 1);
    uint32_t vlen = DecodeFixed32(p + 5);
    p += 9;

    if (p + klen + vlen > limit) {
        valid_ = false;
        return;
    }

    // 填充 current_entry_
    current_entry_.key = std::string(p, klen);
    current_entry_.value = std::string(p + klen, vlen);
    current_entry_.type = static_cast<ValueType>(type_byte);
    current_entry_.sequence = sequence_;

    // 解析完后，立刻将 offset 移动到下一条数据的起点！
    current_offset_ = (p + klen + vlen) - current_block_data_.data();
    valid_ = true;
}

void SSTableIterator::Seek(const std::string& target) {
    if (!reader_ || reader_->GetBlockCount() == 0) {
        valid_ = false;
        return;
    }

    // 1. 通过 Index Block 二分查找目标 target 落在哪个 Data Block
    size_t block_idx = reader_->FindBlockIndex(target);
    if (block_idx >= reader_->GetBlockCount()) {
        valid_ = false;
        return;
    }

    // 2. 加载该 Data Block
    current_block_idx_ = block_idx;
    current_block_data_ = reader_->ReadDataBlock(current_block_idx_);
    current_offset_ = 0;
    valid_ = true;

    // 3. 在 Block 内循环解析，直到找到第一个 key >= target 的记录
    while (true) {
        ParseNextEntry(); // 这会解析一条 entry 并把 offset 指向下一条
        if (!valid_) {
            // 当前 block 及后续 block 找完了都没找到
            return; 
        }

        if (current_entry_.key >= target) {
            // 找到了！此时 valid_ 为 true，且 current_entry_ 就是我们要的 key
            // offset 已经自动准备好了准备指向下一条，下次调 Next() 会顺畅接上！
            return;
        }
    }
}

IteratorEntry SSTableIterator::entry() const {
    return current_entry_;
}

// -------------------------------------------------------------------
// SkipListIterator 实现（优化：支持从 lower_bound 开始拷贝，防止全表扫描导致内存暴涨）
// -------------------------------------------------------------------
SkipListIterator::SkipListIterator(const SkipList<std::string, TableValue>* list, size_t sequence, const std::string& lower_bound)
    : sequence_(sequence) {
    if (list != nullptr) {
        // 🌟 改动点：利用跳表的 find_greater_or_equal 直接定位到 lower_bound，避免无脑从头到尾全量拷贝
        auto node = list->find_greater_or_equal(lower_bound);
        while (node != nullptr) {
            IteratorEntry e;
            e.key = node->key;
            e.value = node->value.value;
            e.type = node->value.type;
            e.sequence = sequence_;
            entries_.push_back(std::move(e));
            node = node->forward[0]; // 沿第 0 层链表向后遍历
        }
    }
    index_ = 0;
}

bool SkipListIterator::Valid() const { 
    return index_ < entries_.size(); 
}

void SkipListIterator::Next() {
    if (Valid()) {
        index_++;
    }
}

IteratorEntry SkipListIterator::entry() const {
    if (Valid()) {
        return entries_[index_];
    }
    return IteratorEntry{};
}

void SkipListIterator::Seek(const std::string& target) {
    // 在快照 vector 中用二分查找（std::lower_bound）快速定位第一个 >= target 的元素
    auto it = std::lower_bound(entries_.begin(), entries_.end(), target, 
        [](const IteratorEntry& entry, const std::string& val) {
            return entry.key < val;
        });
    index_ = std::distance(entries_.begin(), it);
}

// -------------------------------------------------------------------
// MergingIterator 实现
// -------------------------------------------------------------------
MergingIterator::MergingIterator(std::vector<std::unique_ptr<Iterator>> children)
    : children_(std::move(children)) {
    for (size_t i = 0; i < children_.size(); ++i) {
        if (children_[i] && children_[i]->Valid()) {
            min_heap_.push(HeapItem{i, children_[i]->entry()});
        }
    }
    FindMin();
}

bool MergingIterator::Valid() const {
    return valid_;
}

IteratorEntry MergingIterator::entry() const {
    return current_entry_;
}

void MergingIterator::Next() {
    if (!valid_ || min_heap_.empty()) {
        valid_ = false;
        return;
    }

    // 1. 记录刚刚输出的 key，用于在循环中清理掉所有残留的旧版本
    std::string last_key = current_entry_.key;

    // 2. 🌟 关键去重：只要堆顶的 key 和刚才输出的是同一个，就不断弹出并推进对应的子迭代器
    while (!min_heap_.empty() && min_heap_.top().entry.key == last_key) {
        auto top = min_heap_.top();
        min_heap_.pop();

        size_t idx = top.child_index;
        children_[idx]->Next();
        if (children_[idx]->Valid()) {
            min_heap_.push(HeapItem{idx, children_[idx]->entry()});
        }
    }

    // 3. 重新寻找下一个合法的最小 Key
    FindMin();
}

void MergingIterator::Seek(const std::string& target) {
    while (!min_heap_.empty()) min_heap_.pop();

    for (size_t i = 0; i < children_.size(); ++i) {
        if (children_[i]) {
            children_[i]->Seek(target);
            if (children_[i]->Valid()) {
                min_heap_.push(HeapItem{i, children_[i]->entry()});
            }
        }
    }
    FindMin();
}

void MergingIterator::FindMin() {
    // 🌟 循环过滤，确保每次选出的堆顶是去重后的最新有效条目
    while (true) {
        if (min_heap_.empty()) {
            valid_ = false;
            return;
        }

        auto top = min_heap_.top();
        current_entry_ = top.entry;
        valid_ = true;

        // 可以在这里检查 tombstone (ValueType::kTypeDeletion) 如果需要上层引擎过滤的话
        // 目前主要解决多版本重复问题：
        // 由于 HeapItem 排序规则是：key 相同的情况下，sequence 大的（更新的）排在堆顶
        // 所以当同一个 key 存在多个版本时，堆顶一定是最新鲜的那一个。
        // 我们直接采纳它，然后把堆里其他相同 key 的“旧版本”顺便通过 Next 消化掉，避免下次又吐出来。
        
        std::string current_key = current_entry_.key;
        min_heap_.pop();

        // 推进产生这个堆顶的子迭代器
        size_t idx = top.child_index;
        children_[idx]->Next();
        if (children_[idx]->Valid()) {
            min_heap_.push(HeapItem{idx, children_[idx]->entry()});
        }

        // 🌟 清理堆中其他相同 key 的旧版本残余
        while (!min_heap_.empty() && min_heap_.top().entry.key == current_key) {
            auto dup_top = min_heap_.top();
            min_heap_.pop();

            size_t dup_idx = dup_top.child_index;
            children_[dup_idx]->Next();
            if (children_[dup_idx]->Valid()) {
                min_heap_.push(HeapItem{dup_idx, children_[dup_idx]->entry()});
            }
        }

        // 成功选出一个不重复的最新 Key，跳出循环返回
        break;
    }
}