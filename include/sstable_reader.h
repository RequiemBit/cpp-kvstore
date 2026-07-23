#ifndef SSTABLE_READER_H
#define SSTABLE_READER_H

#include "slice.h"
#include "sstable_builder.h" // 共享 Footer 和 IndexEntry 结构体
#include <string>
#include <fstream>
#include <vector>
#include <memory>

class SSTableReader {
public:
    // 工厂模式：通过 Open 方式创建，若文件不存在或魔数校验失败则返回 nullptr
    static std::unique_ptr<SSTableReader> Open(const std::string& filename);

    ~SSTableReader();

    // 禁用拷贝
    SSTableReader(const SSTableReader&) = delete;
    SSTableReader& operator=(const SSTableReader&) = delete;

    // 核心点查接口：在磁盘 SSTable 中精准检索 key
    bool Get(const Slice& key, std::string* value);

private:
    explicit SSTableReader(const std::string& filename);

    // 1. 读取 Footer 并校验魔数
    bool ReadFooter(Footer* footer);
    // 2. 加载 Index Block 到内存
    bool LoadIndexBlock(const Footer& footer);
    // 3. 在特定 Data Block 内存缓存区内精确查找 KV
    bool SearchInDataBlock(const std::string& block_data, const Slice& key, std::string* value);

    std::string filename_;
    std::ifstream file_;
    std::vector<IndexEntry> index_entries_; // 驻留内存的索引树/列表
};

#endif // SSTABLE_READER_H