#ifndef SSTABLE_READER_H
#define SSTABLE_READER_H

#include "slice.h"
#include "sstable_builder.h" // 共享 Footer 和 IndexEntry 结构体
#include <unistd.h>
#include <fcntl.h>
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <sys/stat.h>

class SSTableReader {
public:
    // 工厂模式：通过 Open 方式创建，若文件不存在或魔数校验失败则返回 nullptr
    static std::unique_ptr<SSTableReader> Open(const std::string& filename, size_t sequence = 0);

    ~SSTableReader();
    SSTableReader(const std::string& filename, size_t sequence);
    // 禁用拷贝
    SSTableReader(const SSTableReader&) = delete;
    SSTableReader& operator=(const SSTableReader&) = delete;

    // 核心点查接口：在磁盘 SSTable 中精准检索 key
    // 增加获取 ValueType 的 Get 重载
    bool Get(const Slice& key, std::string* value, ValueType* type);

    const std::string& GetFilePath() const { return filename_; }
    
    // 新增：供 Iterator 遍历使用的接口
    size_t GetBlockCount() const;
    std::string ReadDataBlock(size_t index);
    // 找到包含或可能包含 target 的第一个 Data Block 索引
    size_t FindBlockIndex(const std::string& target) const {
        // 在 index_entries_ (即 Index Block 解析出的元数据) 中二分查找
        // 找到第一个 index_entry.max_key >= target 的 block 索引
        auto it = std::lower_bound(index_entries_.begin(), index_entries_.end(), target,
            [](const IndexEntry& entry, const std::string& key) {
                return entry.max_key < key;
            });
        
        if (it == index_entries_.end()) {
            return index_entries_.size(); // 超出范围
        }
        return std::distance(index_entries_.begin(), it);
    }
    // 暴露序列号，供 NewIterator() 调用
    size_t GetSequence() const { return sequence_; }
private:
    explicit SSTableReader(const std::string& filename);

    // 1. 读取 Footer 并校验魔数
    bool ReadFooter(Footer* footer);
    // 2. 加载 Index Block 到内存
    bool LoadIndexBlock(const Footer& footer);
    // 3. 在特定 Data Block 内存缓存区内精确查找 KV

    // 增加 ValueType* type 参数（建议带上默认参数 nullptr）
    bool SearchInDataBlock(const std::string& block_data, 
                        const Slice& key, 
                        std::string* value, 
                        ValueType* type = nullptr);

    std::string filename_;
    int fd_{-1}; // 文件描述符
    std::ifstream file_;
    std::vector<IndexEntry> index_entries_; // 驻留内存的索引树/列表
    // 存储该 SSTable 文件对应的序列号
    size_t sequence_{0};
};

#endif // SSTABLE_READER_H