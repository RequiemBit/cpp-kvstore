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
SSTableIterator::SSTableIterator(SSTableReader* reader, size_t sequence)
    : reader_(reader), sequence_(sequence) {
    if (reader_ && reader_->GetBlockCount() > 0) {
        current_block_idx_ = 0;
        current_offset_ = 0;
        ParseCurrentBlock();
    } else {
        valid_ = false;
    }
}

void SSTableIterator::ParseCurrentBlock() {
    valid_ = false;
    while (current_block_idx_ < reader_->GetBlockCount()) {
        current_block_data_ = reader_->ReadDataBlock(current_block_idx_);
        current_offset_ = 0;
        if (!current_block_data_.empty()) {
            // 解析当前 Block 的第一条数据
            Next();
            if (valid_) {
                return;
            }
        }
        current_block_idx_++;
    }
}

bool SSTableIterator::Valid() const {
    return valid_;
}

void SSTableIterator::Next() {
    if (!reader_) {
        valid_ = false;
        return;
    }

    // 如果当前 Block 数据用尽，换下一个 Block
    while (current_offset_ >= current_block_data_.size()) {
        current_block_idx_++;
        if (current_block_idx_ >= reader_->GetBlockCount()) {
            valid_ = false;
            return;
        }
        current_block_data_ = reader_->ReadDataBlock(current_block_idx_);
        current_offset_ = 0;
    }

    const char* p = current_block_data_.data() + current_offset_;
    const char* limit = current_block_data_.data() + current_block_data_.size();

    // 1. 校验 Header 长度：type (1B) + key_len (4B) + val_len (4B) = 9 字节
    if (p + 9 > limit) {
        valid_ = false;
        return;
    }

    // 2. 严格对齐 Builder 的序列化顺序：
    // [type (1B)][key_len (4B)][val_len (4B)][key_bytes][val_bytes]
    uint8_t type_byte = static_cast<uint8_t>(p[0]);
    uint32_t klen = DecodeFixed32(p + 1);
    uint32_t vlen = DecodeFixed32(p + 5);
    p += 9;

    // 3. 校验 key + value 实体数据长度
    if (p + klen + vlen > limit) {
        valid_ = false;
        return;
    }

    current_entry_.key = std::string(p, klen);
    current_entry_.value = std::string(p + klen, vlen);
    current_entry_.type = static_cast<ValueType>(type_byte);
    current_entry_.sequence = sequence_;

    // 4. 更新 offset 指向下一条记录
    current_offset_ = (p + klen + vlen) - current_block_data_.data();
    valid_ = true;
}

IteratorEntry SSTableIterator::entry() const {
    return current_entry_;
}

// -------------------------------------------------------------------
// MergingIterator 实现
// -------------------------------------------------------------------
MergingIterator::MergingIterator(std::vector<std::unique_ptr<Iterator>> children)
    : children_(std::move(children)) {
    for (size_t i = 0; i < children_.size(); ++i) {
        if (children_[i] && children_[i]->Valid()) {
            min_heap_.push({i, children_[i]->entry()});
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
    if (min_heap_.empty()) {
        valid_ = false;
        return;
    }

    // 弹出堆顶，并让对应的 child iterator 推进到下一条
    auto top = min_heap_.top();
    min_heap_.pop();

    size_t idx = top.child_index;
    children_[idx]->Next();
    if (children_[idx]->Valid()) {
        min_heap_.push({idx, children_[idx]->entry()});
    }

    FindMin();
}

void MergingIterator::FindMin() {
    if (!min_heap_.empty()) {
        valid_ = true;
        current_entry_ = min_heap_.top().entry;
    } else {
        valid_ = false;
    }
}