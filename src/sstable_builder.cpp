#include "sstable_builder.h"
#include <iostream>

SSTableBuilder::SSTableBuilder(const std::string& filename, size_t block_size)
    : filename_(filename), block_size_(block_size), current_offset_(0), finished_(false) {
    file_.open(filename_, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!file_.is_open()) {
        std::cerr << "[SSTableBuilder Error] Failed to create SSTable file: " << filename_ << std::endl;
    }
}

SSTableBuilder::~SSTableBuilder() {
    if (!finished_ && file_.is_open()) {
        file_.close();
    }
}

void SSTableBuilder::Add(const Slice& key, const Slice& value) {
    if (finished_) return;

    // 序列化一条 KV 记录格式: [key_len (4B)][val_len (4B)][key_bytes][val_bytes]
    uint32_t k_len = static_cast<uint32_t>(key.size());
    uint32_t v_len = static_cast<uint32_t>(value.size());

    block_buffer_.append(reinterpret_cast<const char*>(&k_len), sizeof(k_len));
    block_buffer_.append(reinterpret_cast<const char*>(&v_len), sizeof(v_len));
    block_buffer_.append(key.data(), key.size());
    block_buffer_.append(value.data(), value.size());

    // 记录当前 Block 遇到的最新 (也是最大) 的 Key
    last_key_in_block_ = key.to_string();

    // 如果当前 Block 缓冲区大小达到了预设的 block_size_（例如 4KB），切分并 Flush 磁盘
    if (block_buffer_.size() >= block_size_) {
        FlushBlock();
    }
}

void SSTableBuilder::FlushBlock() {
    if (block_buffer_.empty()) return;

    // 1. 记录此 Data Block 的索引信息
    IndexEntry entry;
    entry.max_key = last_key_in_block_;
    entry.offset = current_offset_;
    entry.size = block_buffer_.size();
    index_entries_.push_back(entry);

    // 2. 将 Data Block 写入磁盘
    file_.write(block_buffer_.data(), block_buffer_.size());
    current_offset_ += block_buffer_.size();

    // 3. 重置缓冲区
    block_buffer_.clear();
}

bool SSTableBuilder::Finish() {
    if (finished_) return false;

    // 1. 刷出最后一个可能未满的 Data Block
    FlushBlock();

    // 2. 构建并写入 Index Block
    uint64_t index_offset = current_offset_;
    std::string index_buffer;

    for (const auto& entry : index_entries_) {
        uint32_t key_len = static_cast<uint32_t>(entry.max_key.size());
        index_buffer.append(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        index_buffer.append(entry.max_key.data(), entry.max_key.size());
        index_buffer.append(reinterpret_cast<const char*>(&entry.offset), sizeof(entry.offset));
        index_buffer.append(reinterpret_cast<const char*>(&entry.size), sizeof(entry.size));
    }

    uint64_t index_size = index_buffer.size();
    file_.write(index_buffer.data(), index_buffer.size());
    current_offset_ += index_size;

    // 3. 构建并写入固定 24 字节的 Footer
    Footer footer;
    footer.index_offset = index_offset;
    footer.index_size = index_size;
    footer.magic_number = kSSTableMagicNumber;

    file_.write(reinterpret_cast<const char*>(&footer), sizeof(footer));
    current_offset_ += sizeof(footer);

    // 4. 落盘并关闭
    file_.flush();
    file_.close();
    finished_ = true;

    std::cout << "[SSTableBuilder] Successfully built SSTable: " << filename_
              << " (Size: " << current_offset_ << " bytes, Blocks: " 
              << index_entries_.size() << ")" << std::endl;

    return true;
}