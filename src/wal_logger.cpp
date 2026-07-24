#include "wal_logger.h"
#include <filesystem>
#include <iostream>
#include <unistd.h> // POSIX API: fsync

// --- CRC32 静态计算模块 ---
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
// -------------------------

WalLogger::WalLogger(const std::string& log_path) {
    // 复用 Open() 函数，内部会自动设置 path_ 并打开文件
    if (!Open(log_path)) {
        std::cerr << "[WAL Error] Failed to open log file: " << path_ << std::endl;
    }
}

WalLogger::~WalLogger() {
    // 复用 Close() 函数，确保 flush 后安全关闭
    Close();
}

bool WalLogger::Open(const std::string& log_path) {
    Close(); // 若之前已打开文件，先安全关闭
    path_ = log_path;
    ofs_.open(path_, std::ios::out | std::ios::app | std::ios::binary);
    return ofs_.is_open();
}

void WalLogger::Close() {
    if (ofs_.is_open()) {
        ofs_.flush();
        ofs_.close();
    }
}


// 校验数据是否有效
uint32_t WalLogger::CalculateChecksum(LogHeader header, const Slice& key, const Slice& value) {
    // 核心点：强制把 checksum 置 0 再计算，确保序列化与反序列化时计算依据完全一致
    header.checksum = 0;

    // 分段更新 CRC，直接使用 Slice 的内存地址，全程 0 拷贝
    uint32_t crc = compute_crc32(reinterpret_cast<const char*>(&header), sizeof(header));
    if (!key.empty())   crc = compute_crc32(key.data(), key.size(), crc);
    if (!value.empty()) crc = compute_crc32(value.data(), value.size(), crc);

    return crc ^ 0xFFFFFFFF;
}

// 将一条日志写入磁盘，每条日志结构是：13字节header+变长key+变长value
bool WalLogger::Append(OperationType op, const Slice& key, const Slice& value) {
    if (!ofs_.is_open()) return false;

    LogHeader header;
    header.key_len = static_cast<uint32_t>(key.size());
    header.value_len = static_cast<uint32_t>(value.size());
    header.op_type = op;
    header.checksum = CalculateChecksum(header, key, value);

    // 1. 写入固定大小的 Header
    ofs_.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // 2. 写入变长的 Key 和 Value Payload
    if (!key.empty())   ofs_.write(key.data(), key.size());
    if (!value.empty()) ofs_.write(value.data(), value.size());

    // 3. 刷入内核 Page Cache
    ofs_.flush();

    return ofs_.good();
}


// 通过wallog恢复数据
std::vector<ParsedLogRecord> WalLogger::Recover() {
    std::vector<ParsedLogRecord> records;

    std::ifstream ifs(path_, std::ios::binary | std::ios::in);
    if (!ifs.is_open()) {
        std::cout << "[WAL Recovery] No log file found. Starting fresh." << std::endl;
        return records;
    }

    while (ifs.peek() != EOF) {
        LogHeader header;

        // 尝试读取固定大小 Header
        ifs.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (ifs.gcount() < static_cast<std::streamsize>(sizeof(header))) {
            std::cout << "[WAL Recovery] Reached EOF or partial header. Stopping recovery." << std::endl;
            break;
        }

        // 读取 Key 和 Value 载体
        std::string key(header.key_len, '\0');
        std::string value(header.value_len, '\0');

        if (header.key_len > 0)   ifs.read(&key[0], header.key_len);
        if (header.value_len > 0) ifs.read(&value[0], header.value_len);

        // 校验 CRC32 数据的有效性
        uint32_t expected_crc = CalculateChecksum(header, Slice(key), Slice(value));
        if (header.checksum != expected_crc) {
            std::cerr << "[WAL Recovery Error] Checksum mismatch! Stopping at corrupted record." << std::endl;
            break; // 遇到了坏数据块，直接截断停止恢复
        }

        // 校验成功，填充解析出的日志记录
        records.push_back({header.op_type, key, value});
    }

    ifs.close();
    std::cout << "[WAL Recovery] Successfully recovered " << records.size() << " records." << std::endl;
    return records;
}

void WalLogger::Sync() {
    if (ofs_.is_open()) {
        ofs_.flush();
    }
}

bool WalLogger::RemoveWalFile(const std::string& log_path) {
    std::error_code ec;
    return std::filesystem::remove(log_path, ec);
}