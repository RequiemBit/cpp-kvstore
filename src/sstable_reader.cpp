#include "sstable_reader.h"
#include <iostream>
#include <algorithm>

SSTableReader::SSTableReader(const std::string& filename) : filename_(filename) {}

SSTableReader::~SSTableReader() {
    if (file_.is_open()) {
        file_.close();
    }
}

std::unique_ptr<SSTableReader> SSTableReader::Open(const std::string& filename) {
    auto reader = std::unique_ptr<SSTableReader>(new SSTableReader(filename));
    reader->file_.open(filename, std::ios::binary | std::ios::in);
    if (!reader->file_.is_open()) {
        std::cerr << "[SSTableReader Error] Cannot open file: " << filename << std::endl;
        return nullptr;
    }

    // 1. 解析 Footer
    Footer footer;
    if (!reader->ReadFooter(&footer)) {
        std::cerr << "[SSTableReader Error] Invalid footer or magic number mismatch!" << std::endl;
        return nullptr;
    }

    // 2. 加载 Index Block
    if (!reader->LoadIndexBlock(footer)) {
        std::cerr << "[SSTableReader Error] Failed to load index block!" << std::endl;
        return nullptr;
    }

    return reader;
}

bool SSTableReader::ReadFooter(Footer* footer) {
    // 定位到文件末尾倒数 24 字节 (sizeof(Footer))
    file_.seekg(0, std::ios::end);
    uint64_t file_size = file_.tellg();
    if (file_size < sizeof(Footer)) return false;

    file_.seekg(file_size - sizeof(Footer), std::ios::beg);
    file_.read(reinterpret_cast<char*>(footer), sizeof(Footer));

    // 校验魔数
    return footer->magic_number == kSSTableMagicNumber;
}

bool SSTableReader::LoadIndexBlock(const Footer& footer) {
    file_.seekg(footer.index_offset, std::ios::beg);
    std::string index_buffer(footer.index_size, '\0');
    file_.read(&index_buffer[0], footer.index_size);

    if (file_.gcount() < static_cast<std::streamsize>(footer.index_size)) {
        return false;
    }

    // 反序列化 Index Block 填充 index_entries_
    size_t cursor = 0;
    while (cursor < index_buffer.size()) {
        uint32_t key_len = 0;
        std::memcpy(&key_len, index_buffer.data() + cursor, sizeof(key_len));
        cursor += sizeof(key_len);

        std::string max_key(index_buffer.data() + cursor, key_len);
        cursor += key_len;

        uint64_t offset = 0;
        std::memcpy(&offset, index_buffer.data() + cursor, sizeof(offset));
        cursor += sizeof(offset);

        uint64_t size = 0;
        std::memcpy(&size, index_buffer.data() + cursor, sizeof(size));
        cursor += sizeof(size);

        index_entries_.push_back({max_key, offset, size});
    }

    return true;
}

bool SSTableReader::Get(const Slice& key, std::string* value) {
    if (index_entries_.empty()) return false;

    // 1. Index 内存层二分查找：利用 std::lower_bound 定位目标 Block
    auto it = std::lower_bound(
        index_entries_.begin(), 
        index_entries_.end(), 
        key, 
        [](const IndexEntry& entry, const Slice& k) {
            return Slice(entry.max_key) < k;
        }
    );

    // 如果 target_key 比整个 SSTable 里的所有 max_key 都大，说明绝对不存在
    if (it == index_entries_.end()) {
        return false;
    }

    // 2. IO 读取对应的 Data Block
    file_.seekg(it->offset, std::ios::beg);
    std::string block_data(it->size, '\0');
    file_.read(&block_data[0], it->size);

    // 3. 在读取到的 Data Block 内精准检索
    return SearchInDataBlock(block_data, key, value);
}

bool SSTableReader::SearchInDataBlock(const std::string& block_data, const Slice& key, std::string* value) {
    size_t cursor = 0;
    while (cursor < block_data.size()) {
        uint32_t k_len = 0;
        uint32_t v_len = 0;

        std::memcpy(&k_len, block_data.data() + cursor, sizeof(k_len));
        cursor += sizeof(k_len);
        std::memcpy(&v_len, block_data.data() + cursor, sizeof(v_len));
        cursor += sizeof(v_len);

        Slice current_key(block_data.data() + cursor, k_len);
        cursor += k_len;
        Slice current_val(block_data.data() + cursor, v_len);
        cursor += v_len;

        // 精确匹配
        if (current_key == key) {
            if (value) *value = current_val.to_string();
            return true;
        }
        
        // 剪枝优化：由于 Block 内部 Key 也是按字典序升序存储的
        // 如果当前 key 已经大于目标 key，说明后面的都不可能匹配，提前退出
        if (current_key > key) {
            break;
        }
    }
    return false;
}