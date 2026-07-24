#ifndef SSTABLE_BUILDER_H
#define SSTABLE_BUILDER_H

#include "slice.h"
#include <string>
#include <fstream>
#include <vector>
#include <cstdint>

// SSTable 魔数，用于识别和校验文件格式 (0xdb4775248b80fb57ULL)
constexpr uint64_t kSSTableMagicNumber = 0xdb4775248b80fb57ULL;

// Footer 结构体：固定存储在 SSTable 文件末尾
#pragma pack(push, 1)
struct Footer {
    uint64_t index_offset; // Index Block 在文件中的偏移量
    uint64_t index_size;   // Index Block 的字节大小
    uint64_t magic_number; // 校验魔数
};
#pragma pack(pop)

// 索引条目：记录某一个 Data Block 的边界与位置
struct IndexEntry {
    std::string max_key; // 该 Block 中的最大 Key
    uint64_t offset;     // 该 Block 在文件中的起始 Offset
    uint64_t size;       // 该 Block 的字节长度
};

class SSTableBuilder {
public:
    // 构造时传入要生成的 SSTable 文件路径，以及单个 Block 的最大字节数（默认 4KB）
    explicit SSTableBuilder(const std::string& filename, size_t block_size = 4096);
    ~SSTableBuilder();

    // 禁用拷贝
    SSTableBuilder(const SSTableBuilder&) = delete;
    SSTableBuilder& operator=(const SSTableBuilder&) = delete;

    // 追加一个 KV 对 (注意：传入的 Key 必须严格递增/有序)
    // 增加带 ValueType 的重载/统一接口
    void Add(const Slice& key, const Slice& value, ValueType type);
    void Add(const Slice& key, const Slice& value); // 兼容旧接口
    // 完成 SSTable 的构建（刷入最后的 Data Block、写入 Index Block 和 Footer）
    bool Finish();

    // 获取当前已写入的总字节数
    uint64_t FileSize() const { return current_offset_; }

private:
    void FlushBlock(); // 将当前 block_buffer_ 中的数据刷入文件并生成 IndexEntry

    std::string filename_;
    std::ofstream file_;
    size_t block_size_;      // 单个 Block 的目标阈值 (例如 4KB)
    uint64_t current_offset_;// 当前文件写入指针偏移量

    std::string block_buffer_;        // 当前正在构建的 Data Block 缓冲区
    std::string last_key_in_block_;   // 当前 Block 中最大的 Key
    std::vector<IndexEntry> index_entries_; // 收集所有的索引项
    bool finished_;                   // 是否已调用 Finish()
};

#endif // SSTABLE_BUILDER_H


// sstable_table 作用