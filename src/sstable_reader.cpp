#include "sstable_reader.h"

SSTableReader::SSTableReader(const std::string& filename, size_t sequence) : filename_(filename) , sequence_(sequence){}

SSTableReader::~SSTableReader() {
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
    }
}

std::unique_ptr<SSTableReader> SSTableReader::Open(const std::string& filename, size_t sequence) {
    // 1. 使用 new 创建私有构造的实例，并传入 sequence
    auto reader = std::unique_ptr<SSTableReader>(new SSTableReader(filename, sequence));
    
    // 2. 使用 POSIX open 获取文件描述符
    reader->fd_ = open(filename.c_str(), O_RDONLY);
    if (reader->fd_ == -1) {
        std::cerr << "[SSTableReader Error] Cannot open file: " << filename << std::endl;
        return nullptr;
    }

    // 3. 解析 Footer
    Footer footer;
    if (!reader->ReadFooter(&footer)) {
        std::cerr << "[SSTableReader Error] Invalid footer or magic number mismatch in " 
                  << filename << std::endl;
        close(reader->fd_); // 🌟 注意：失败时及时关闭 fd，防止描述符泄漏
        reader->fd_ = -1;
        return nullptr;
    }

    // 4. 加载 Index Block
    if (!reader->LoadIndexBlock(footer)) {
        std::cerr << "[SSTableReader Error] Failed to load index block from " 
                  << filename << std::endl;
        close(reader->fd_); // 🌟 失败时及时关闭 fd
        reader->fd_ = -1;
        return nullptr;
    }

    // 5. 校验通过，返回初始化成功的 unique_ptr
    return reader;
}

// 读取 footer 的信息，使用 pread 替代 seekg + read
bool SSTableReader::ReadFooter(Footer* footer) {
    struct stat st;
    if (fstat(fd_, &st) != 0) {
        return false;
    }
    
    uint64_t file_size = st.st_size;
    if (file_size < sizeof(Footer)) {
        return false;
    }

    uint64_t footer_offset = file_size - sizeof(Footer);
    ssize_t bytes_read = pread(fd_, reinterpret_cast<char*>(footer), sizeof(Footer), footer_offset);
    if (bytes_read != static_cast<ssize_t>(sizeof(Footer))) {
        return false;
    }

    return footer->magic_number == kSSTableMagicNumber;
}

// 根据 footer 加载 index_block 到内存 (index_entries_) 中
bool SSTableReader::LoadIndexBlock(const Footer& footer) {
    std::string index_buffer(footer.index_size, '\0');
    ssize_t bytes_read = pread(fd_, &index_buffer[0], footer.index_size, footer.index_offset);

    if (bytes_read != static_cast<ssize_t>(footer.index_size)) {
        return false;
    }

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

// 能识别 Tombstone 并返回类型，这里是确定数据在哪个 datablock，再调用 SearchInDataBlock 去查
bool SSTableReader::Get(const Slice& key, std::string* value, ValueType* type) {
    if (index_entries_.empty()) return false;

    // 1. Index 层 lower_bound 查找对应 Block
    auto it = std::lower_bound(
        index_entries_.begin(), 
        index_entries_.end(), 
        key, 
        [](const IndexEntry& entry, const Slice& k) {
            return Slice(entry.max_key) < k;
        }
    );

    if (it == index_entries_.end()) {
        return false;
    }

    // 2. 使用 POSIX pread 进行无状态并发读取
    std::string block_data(it->size, '\0');
    ssize_t bytes_read = pread(fd_, &block_data[0], it->size, it->offset);
    if (bytes_read != static_cast<ssize_t>(it->size)) {
        return false; // 读取失败防御
    }

    // 3. 在 Data Block 内检索并获取 Value 和 ValueType
    return SearchInDataBlock(block_data, key, value, type);
}

// 在 DataBlock 内部检索 Key
bool SSTableReader::SearchInDataBlock(const std::string& block_data, const Slice& key, std::string* value, ValueType* type) {
    size_t cursor = 0;
    while (cursor < block_data.size()) {
        // 解析: [type (1B)][key_len (4B)][val_len (4B)][key_bytes][val_bytes]
        uint8_t raw_type = 0;
        uint32_t k_len = 0;
        uint32_t v_len = 0;

        std::memcpy(&raw_type, block_data.data() + cursor, sizeof(raw_type));
        cursor += sizeof(raw_type);

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
            if (type) *type = static_cast<ValueType>(raw_type);
            if (value && raw_type == static_cast<uint8_t>(ValueType::kTypeValue)) {
                *value = current_val.to_string();
            } else if (value) {
                value->clear(); // 若为 Tombstone，清空传入的 string
            }
            return true;
        }
        
        // Key 升序剪枝
        if (current_key > key) {
            break;
        }
    }
    return false;
}

// 获取 Data Block 的总数量
size_t SSTableReader::GetBlockCount() const {
    return index_entries_.size();
}

// 读取指定索引的 Data Block 原始字节数据 (供 Iterator/Compaction 使用)
std::string SSTableReader::ReadDataBlock(size_t index) {
    if (index >= index_entries_.size()) {
        return "";
    }

    const auto& index_item = index_entries_[index];
    std::string block_data(index_item.size, '\0');

    ssize_t bytes_read = pread(fd_, &block_data[0], index_item.size, index_item.offset);
    if (bytes_read != static_cast<ssize_t>(index_item.size)) {
        return "";
    }

    return block_data;
}