#include "wal_logger.h"
#include <iostream>
#include <cstring>
#include <unistd.h> // 引入 POSIX 系统调用 (fsync)

// --- CRC32 实现 ---
static uint32_t crc32_table[256];
static bool crc32_initialized = false;

static void init_crc32() {
    if (crc32_initialized) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
        crc32_table[i] = crc;
    }
    crc32_initialized = true;
}

static uint32_t compute_crc32(const char* data, size_t length, uint32_t previous_crc = 0xFFFFFFFF) {
    init_crc32();
    uint32_t crc = previous_crc;
    for (size_t i = 0; i < length; ++i) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ static_cast<unsigned char>(data[i])) & 0xFF];
    }
    return crc;
}

// ------------------

// 构造函数
WalLogger::WalLogger(const std::string& log_path) : path_(log_path) {
    ofs_.open(path_, std::ios::binary | std::ios::app | std::ios::out);
    if (!ofs_.is_open()) {
        std::cerr << "[WAL Error] Cannot open log file: " << path_ << std::endl;
    }
}

// 析构函数
WalLogger::~WalLogger() {
    if (ofs_.is_open()) {
        ofs_.close();
    }
}


// 算 CRC32 辅助函数
uint32_t WalLogger::CalculateChecksum(LogHeader header, const std::string& key, const std::string& value) {
    // 强制把校验和置为 0 再算，保证 append 和 recover 算出来的 CRC 完全一致！
    header.checksum = 0;
    
    // 分流计算，无需创建 vector 分配内存
    uint32_t crc = compute_crc32(reinterpret_cast<const char*>(&header), sizeof(header));
    if (!key.empty())   crc = compute_crc32(key.data(), key.size(), crc);
    if (!value.empty()) crc = compute_crc32(value.data(), value.size(), crc);
    
    return crc ^ 0xFFFFFFFF;
}

// 将一条日志写入磁盘
// 实际写入的每一条日志的结构是：13字节header+变长的key+value
bool WalLogger::Append(OperationType op, const std::string& key, const std::string& value) {
    if (!ofs_.is_open()) return false;

    LogHeader header;
    header.key_len = static_cast<uint32_t>(key.size());
    header.value_len = static_cast<uint32_t>(value.size());
    header.op_type = op;
    header.checksum = CalculateChecksum(header, key, value);

    // 1. 写入 Header
    ofs_.write(reinterpret_cast<const char*>(&header), sizeof(header));
    
    // 2. 写入 Key & Value
    if (!key.empty())   ofs_.write(key.data(), key.size());
    if (!value.empty()) ofs_.write(value.data(), value.size());

    ofs_.flush(); // 用户态缓存刷到内核态 Page Cache

    return ofs_.good();
}

// 读取日志进行恢复
std::vector<ParsedLogRecord> WalLogger::Recover() {
    std::vector<ParsedLogRecord> records;
    
    // 所有日志按照13字节紧密排列，通过path_路径读取
    std::ifstream ifs(path_, std::ios::binary | std::ios::in);
    if (!ifs.is_open()) {
        std::cout << "[WAL Recovery] No log file found, starting fresh." << std::endl;
        return records;
    }

    while (ifs.peek() != EOF) {
        LogHeader header;
        
        // 读取 Header
        ifs.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (ifs.gcount() < static_cast<std::streamsize>(sizeof(header))) {
            std::cout << "[WAL Recovery] Incomplete header detected, stopping recovery." << std::endl;
            break; 
        }

        // 读取 Key 和 Value
        std::string key(header.key_len, '\0');
        std::string value(header.value_len, '\0');
        
        if (header.key_len > 0)   ifs.read(&key[0], header.key_len);
        if (header.value_len > 0) ifs.read(&value[0], header.value_len);

        // 校验完整性
        uint32_t expected_crc = CalculateChecksum(header, key, value);
        if (header.checksum != expected_crc) {
            std::cerr << "[WAL Recovery] Checksum mismatch! Log file corrupted at offset. Stopping." << std::endl;
            break;
        }

        // 组装完整数据返回
        records.push_back({header.op_type, key, value});
    }

    ifs.close();
    std::cout << "[WAL Recovery] Successfully recovered " << records.size() << " records." << std::endl;
    return records;
}