#ifndef WAL_LOGGER_H
#define WAL_LOGGER_H

#include "slice.h"
#include <string>
#include <fstream>
#include <cstdint>
#include <vector>

// 强类型枚举，设定只占用一个字节，1表示put，2表示erase
// unit8_t作用
// 如果不优化：你可能要在日志里写字符串 "PUT" (3字节) + "name" + "requiem"。
// 用了这个枚举：你在日志里只写 0x01 (1字节) + "name" + "requiem"
// 减少了内存开销
enum class OperationType : uint8_t {
    PUT = 0x01,
    ERASE = 0x02
};

// pack(push,1)作用
// 防止内存对齐，如果对齐到16会塞三个字节的垃圾数据造成错误
// CRC32效验码用于记录这条数据是否有效
#pragma pack(push, 1)
struct LogHeader {
    uint32_t checksum;   // 4 字节: CRC32
    uint32_t key_len;    // 4 字节，表示key长度
    uint32_t value_len;  // 4 字节，表示value长度
    OperationType op_type; // 1 字节，表示put还是erase
};
#pragma pack(pop)

// 用于恢复时返回给 SkipList 的完整数据载体
struct ParsedLogRecord {
    OperationType op;
    std::string key;
    std::string value;
};

class WalLogger {
public:
    explicit WalLogger(const std::string& log_path);
    ~WalLogger();

    // 写入一条日志到磁盘（升级为 Slice 参数，支持零拷贝）
    bool Append(OperationType op, const Slice& key, const Slice& value = Slice());

    // 从磁盘读取所有有效日志用于恢复
    std::vector<ParsedLogRecord> Recover();

    // 真正物理落盘 (FSYNC)
    void Sync();

private:
    // 日志存放的路径
    std::string path_;
    // 写入操作的对象
    std::ofstream ofs_;
    
    // 算 CRC32 辅助函数（保证 checksum = 0 计算）
    uint32_t CalculateChecksum(LogHeader header, const Slice& key, const Slice& value);
};

#endif // WAL_LOGGER_H