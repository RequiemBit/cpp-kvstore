#include "sstable_builder.h"
#include <iostream>

// 构建buidler，这个时候创建了空的sst文件
// -> Add(K,V) 向缓冲区加入一条数据，这个时候数据在内存中
// -> 缓冲区满了 -> FlushBlock() 
//              -> 将一个真正的block写入磁盘(sst文件内)，清空缓冲区
// -> Finish()  -> 将缓冲区剩余数据写入sst,构建index_block和Footer,写入sst
// 这个时候，一个完整的sst文件就完成了

// 构造函数，这个时候磁盘中就有sst文件了，只是没有数据
SSTableBuilder::SSTableBuilder(const std::string& filename, size_t block_size)
    : filename_(filename), block_size_(block_size), current_offset_(0), finished_(false) {
    file_.open(filename_, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file_.is_open()) {
        std::cerr << "[SSTableBuilder Error] Failed to create SSTable file: " << filename_ << std::endl;
    }
}

// 析构
SSTableBuilder::~SSTableBuilder() {
    if (!finished_ && file_.is_open()) {
        file_.close();
    }
}

// 将一条数据添加进datablock，满了FlushBlock()构建完整的datablock
void SSTableBuilder::Add(const Slice& key, const Slice& value, ValueType type) {
    if (finished_) return;

    // 1. 序列化格式: [type (1B)][key_len (4B)][val_len (4B)][key_bytes][val_bytes]
    uint8_t raw_type = static_cast<uint8_t>(type);
    uint32_t k_len = static_cast<uint32_t>(key.size());
    uint32_t v_len = (type == ValueType::kTypeDeletion) ? 0 : static_cast<uint32_t>(value.size());

    block_buffer_.append(reinterpret_cast<const char*>(&raw_type), sizeof(raw_type));
    block_buffer_.append(reinterpret_cast<const char*>(&k_len), sizeof(k_len));
    block_buffer_.append(reinterpret_cast<const char*>(&v_len), sizeof(v_len));
    
    block_buffer_.append(key.data(), key.size());
    if (v_len > 0) {
        block_buffer_.append(value.data(), v_len);
    }

    // 2. 记录当前 Block 的最大 Key
    last_key_in_block_ = key.to_string();

    // 3. 缓冲区满了则刷盘
    if (block_buffer_.size() >= block_size_) {
        FlushBlock();
    }
}

// 兼容旧接口的 Add 重载
void SSTableBuilder::Add(const Slice& key, const Slice& value) {
    Add(key, value, ValueType::kTypeValue);
}

// 真正构建datablock，并且将datablock写入write磁盘
void SSTableBuilder::FlushBlock() {
    // 缓冲区为空，不需要写入datablock
    if (block_buffer_.empty()) return;

    // 1. 记录 Index 索引数据
    IndexEntry entry;
    entry.max_key = last_key_in_block_;
    entry.offset = current_offset_;
    entry.size = block_buffer_.size();
    index_entries_.push_back(entry);

    // 2. 写入磁盘 Data Block
    file_.write(block_buffer_.data(), block_buffer_.size());
    current_offset_ += block_buffer_.size();

    // 3. 清空 Block 缓冲区
    block_buffer_.clear();
}

// 构建indexblock和footer，
bool SSTableBuilder::Finish() {
    if (finished_) return false;

    // 1. 如果内存缓冲区 block_buffer_ 里还有没刷盘的尾部数据，先刷盘
    if (!block_buffer_.empty()) {
        FlushBlock();
    }

    // 检查是否有数据写入
    if (index_entries_.empty()) {
        finished_ = true;
        file_.close();
        return false;
    }

    // 2. 构建并写入 Index Block
    uint64_t index_offset = current_offset_;
    for (const auto& entry : index_entries_) {
        uint32_t k_len = static_cast<uint32_t>(entry.max_key.size());
        uint64_t offset = entry.offset;
        uint64_t size = entry.size;

        file_.write(reinterpret_cast<const char*>(&k_len), sizeof(k_len));
        file_.write(entry.max_key.data(), k_len);
        file_.write(reinterpret_cast<const char*>(&offset), sizeof(offset));
        file_.write(reinterpret_cast<const char*>(&size), sizeof(size));

        // 累加 current_offset_，这样才能正确计算出 index_size！
        current_offset_ += (sizeof(k_len) + k_len + sizeof(offset) + sizeof(size));
    }
    uint64_t index_size = current_offset_ - index_offset;

    // 补充完整的 写入 Footer 逻辑！
    Footer footer;
    footer.index_offset = index_offset;
    footer.index_size = index_size;
    footer.magic_number = kSSTableMagicNumber;

    file_.write(reinterpret_cast<const char*>(&footer), sizeof(Footer));

    file_.flush();
    file_.close();
    finished_ = true;
    return true;
}