#include "kv_engine.h"
#include "iterator.h"

#include <filesystem>
#include <iostream>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include "iterator.h"

// 构造engine的过程
KVEngine::KVEngine(const std::string& db_path, size_t max_mem_nodes, int max_level)
    : skiplist_(max_level),
      db_path_(db_path),
      max_mem_nodes_(max_mem_nodes) {
    
    std::lock_guard<std::shared_mutex> lock(rw_mutex_);

    // 1. 确保数据目录存在
    if (!std::filesystem::exists(db_path_)) {
        std::filesystem::create_directories(db_path_);
    }

    // 2. 加载磁盘上已存在的 SSTable 文件
    LoadExistingSSTables();

    // 3. 重放历史 WAL 日志进行崩溃恢复
    RecoverAllWals();

    // 4. 为新的内存写操作初始化全新的 WAL 文件
    current_wal_path_ = GetWalPath(sst_counter_ + 1);
    wal_.Open(current_wal_path_);

    // std::cout << "[KVEngine] Recovery complete. Engine ready." << std::endl;
}

// 析构时候调用wal的Close保证wal文件落盘
KVEngine::~KVEngine() {
    std::lock_guard<std::shared_mutex> lock(rw_mutex_);
    wal_.Close();
}

// wallog日志的路径，路径+文件名
std::string KVEngine::GetWalPath(size_t seq_num) const {
    std::ostringstream oss;
    oss << db_path_ << "/wal_" << std::setw(6) << std::setfill('0') << seq_num << ".log";
    return oss.str();
}

// sst文件的路径，路径+文件名
std::string KVEngine::GetSstPath(size_t seq_num) const {
    std::ostringstream oss;
    oss << db_path_ << "/" << std::setw(6) << std::setfill('0') << seq_num << ".sst";
    return oss.str();
}

// 插入一条数据，先写日志，再写跳表，超过阈值触发刷盘
void KVEngine::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::shared_mutex> lock(rw_mutex_);
    
    // 1. 先落 WAL 日志
    wal_.Append(OperationType::PUT, key, value);

    // 2. 更新内存 MemTable
    skiplist_.put(key, TableValue{ValueType::kTypeValue, value});

    // 3. 容量检测：超限则触发 Flush 刷盘
    if (skiplist_.size() >= max_mem_nodes_) {
        FlushMemTable();
    }
}

// 查数据，先从内存找，再从sst中找
// 查数据，先从内存找，再从sst中找
bool KVEngine::get(const std::string& key, std::string& value) const {
    std::shared_lock<std::shared_mutex> lock(rw_mutex_);

    // 1. 第一优先级：检索 MemTable (内存)
    TableValue mem_val;
    if (skiplist_.get(key, mem_val)) {
        if (mem_val.type == ValueType::kTypeDeletion) {
            return false; // 命中内存墓碑！说明该 Key 在最新的操作中已被删除
        }
        value = mem_val.value;
        return true; // 找到正常值
    }

    // 2. 第二优先级：检索 SSTables (磁盘，从最新到最旧)
    for (const auto& reader : sstables_) {
        ValueType type;
        std::string val;
        
        // 关键改动：传入 ValueType 指针以识别 SSTable 内部的墓碑标记
        if (reader->Get(Slice(key), &val, &type)) {
            if (type == ValueType::kTypeDeletion) {
                return false; // 命中磁盘墓碑！终止向下层 SSTable 穿透检索
            }
            value = val; // 命中有效数据
            return true;
        }
    }

    return false; // 内存和磁盘均未找到
}

// 删除一个键值，实际上是特殊的put操作
bool KVEngine::erase(const std::string& key) {
    std::lock_guard<std::shared_mutex> lock(rw_mutex_);

    // 1. 先写 WAL 记录删除操作
    wal_.Append(OperationType::ERASE, key, "");

    // 2. 核心修改：往 MemTable 插入一个 kTypeDeletion 墓碑节点！
    // 无论这个 Key 之前在磁盘的哪个 SSTable 里，这个墓碑都会在 Read Path 中优先被查到并挡住它
    skiplist_.put(key, TableValue{ValueType::kTypeDeletion, ""});

    return true;
}

// 手动调用刷盘
void KVEngine::force_flush() {
    std::lock_guard<std::shared_mutex> lock(rw_mutex_);
    FlushMemTable();
}

// 用于测试
void KVEngine::debug_print() const {
    std::lock_guard<std::shared_mutex> lock(rw_mutex_);
    skiplist_.display();
}

// 刷盘，将内存的skiplist的所有键和值通过builder的add加入datablock中
void KVEngine::FlushMemTable() {
    if (skiplist_.size() == 0) return;

    ++sst_counter_;
    std::string sst_path = GetSstPath(sst_counter_);

    // std::cout << "[KVEngine] MemTable limit reached. Flushing to " << sst_path << "..." << std::endl;

    SSTableBuilder builder(sst_path);
    auto all_kv = skiplist_.dump_all(); 
    for (const auto& [k, v] : all_kv) {
        // 关键改动：将 v.type (kTypeValue 或 kTypeDeletion) 传入 SSTableBuilder
        builder.Add(Slice(k), Slice(v.value), v.type);
    }
    builder.Finish();

    auto reader = SSTableReader::Open(sst_path);
    if (reader) {
        sstables_.insert(sstables_.begin(), std::move(reader));
    }

    // --- WAL 轮转与删除废弃 WAL ---
    std::string old_wal_path = current_wal_path_;
    current_wal_path_ = GetWalPath(sst_counter_ + 1);
    wal_.Open(current_wal_path_);

    skiplist_.clear();

    WalLogger::RemoveWalFile(old_wal_path);

    // std::cout << "[KVEngine] Flush complete. SSTables active count: " << sstables_.size() << std::endl;
}

// 加载现有的sst文件，将reader对象指针加入sstables_内，每个reader维护了index_entries(index_block内的内容)
void KVEngine::LoadExistingSSTables() {
    std::vector<std::string> sst_files;
    for (const auto& entry : std::filesystem::directory_iterator(db_path_)) {
        if (entry.path().extension() == ".sst") {
            sst_files.push_back(entry.path().string());
            
            // 提取最大序号更新 sst_counter_
            std::string filename = entry.path().stem().string();
            try {
                size_t seq = std::stoull(filename);
                sst_counter_ = std::max(sst_counter_, seq);
            } catch (...) {}
        }
    }

    std::sort(sst_files.begin(), sst_files.end());

    for (auto it = sst_files.rbegin(); it != sst_files.rend(); ++it) {
        auto reader = SSTableReader::Open(*it);
        if (reader) {
            sstables_.push_back(std::move(reader));
        }
    }
}

// 通过日志恢复数据
void KVEngine::RecoverAllWals() {
    std::vector<std::string> wal_files;
    for (const auto& entry : std::filesystem::directory_iterator(db_path_)) {
        if (entry.path().extension() == ".log" && 
            entry.path().filename().string().rfind("wal_", 0) == 0) {
            wal_files.push_back(entry.path().string());
        }
    }
    
    // 按编号升序重放
    std::sort(wal_files.begin(), wal_files.end());

    for (const auto& wal_path : wal_files) {
        WalLogger recovery_wal(wal_path);
        auto records = recovery_wal.Recover();
        for (const auto& record : records) {
            if (record.op == OperationType::PUT) {
                skiplist_.put(record.key, TableValue(ValueType::kTypeValue, record.value));
            } else if (record.op == OperationType::ERASE) {
                skiplist_.put(record.key, TableValue(ValueType::kTypeDeletion, ""));
            }
        }
        recovery_wal.Close();
    }
}

// 归并排序合并sst文件
void KVEngine::Compact() {
    namespace fs = std::filesystem; // 仅在 Compact() 函数内部生效，干净且隔离
    std::lock_guard<std::shared_mutex> lock(rw_mutex_);

    // 如果 SSTable 数量小于等于 1，无需压缩
    if (sstables_.size() <= 1) {
        return;
    }

    // std::cout << "[Compaction] Starting Major Compaction for " << sstables_.size() << " SSTables..." << std::endl;

    // 1. 构建所有 SSTable 的 Iterator 向量
    // 注意：sstables_ 中 index 0 是最新生成的，序列号最大
    std::vector<std::unique_ptr<Iterator>> iterators;
    size_t total_ssts = sstables_.size();
    for (size_t i = 0; i < total_ssts; ++i) {
        // 给每一个 SSTable 赋予 sequence，索引 0 sequence 最大
        size_t seq = total_ssts - i; 
        // 修改点：传入 sstables_[i].get() 指针
        iterators.push_back(std::make_unique<SSTableIterator>(sstables_[i].get(), seq));
    }

    // 2. 初始化多路归并迭代器
    MergingIterator merge_iter(std::move(iterators));

    // 3. 创建合并后的目标 SSTable
    ++sst_counter_;
    std::string compact_sst_path = GetSstPath(sst_counter_);
    SSTableBuilder builder(compact_sst_path);

    std::string last_key = "";
    bool has_last_key = false;
    size_t compacted_records = 0;
    size_t dropped_records = 0;

    // 4. 遍历归并数据流（去重 + 墓碑清理）
    while (merge_iter.Valid()) {
        auto entry = merge_iter.entry();

        // 由于归并流按 Key 排序（同 Key 按 sequence 从大到小），
        // 遇到同 Key 时，第一条必定是最新的 Version/墓碑！
        if (has_last_key && entry.key == last_key) {
            // 抛弃旧版本数据
            dropped_records++;
            merge_iter.Next();
            continue;
        }

        last_key = entry.key;
        has_last_key = true;

        // 如果最新版本是墓碑，彻底清理（因为这是 Major Compaction 包含全量 SSTable）
        if (entry.type == ValueType::kTypeDeletion) {
            dropped_records++; // 墓碑本身及其旧版本均丢弃
            merge_iter.Next();
            continue;
        }

        // 有效最新数据落盘
        builder.Add(Slice(entry.key), Slice(entry.value), entry.type);
        compacted_records++;
        merge_iter.Next();
    }

    builder.Finish();

    // 5. 替换 SSTables 句柄与磁盘物理文件清理
    std::vector<std::string> old_sst_paths;
    // 假设全部旧 SSTable 均参与了 Compaction，我们可以通过遍历磁盘或者由 SSTableReader 记录路径清理
    // 这里简单清理原参与归并的 SSTable 内存句柄
    sstables_.clear();

    // 打开压缩后生成的新 SSTable
    auto reader = SSTableReader::Open(compact_sst_path);
    if (reader) {
        sstables_.push_back(std::move(reader));
    }

    // std::cout << "[Compaction] Finished! Saved " << compacted_records 
    //           << " active records, dropped " << dropped_records 
    //           << " obsolete/tombstone records." << std::endl;
}

// 启动一个kvengine -> LoadExistingSSTables加载原有sst文件，RecoverAllWals日志恢复数据，open创建新wal文件

// 			   ->  put(K,V)存入一条数据 -> wal_.Append追加日志，日志直接进入磁盘
// 					  				 -> skiplist_.put,数据存入内存
// 					  				 -> if (skiplist_.size() >= max_mem_nodes_)超过设置阈值，触发刷盘
// 					  				 -> FlushMemTable() -> 增加sst计数，遍历skiplist依次add，调用    																	builder.Finish()生成完整的一个sst
// 					  				 				  -> 将新数据的index_block用reader加载到内存???
// 					  				 				  -> 删除废弃的wal文件，清空skiplist缓存
// 			   -> erase(K)删除一条数据  ->  先wal_.Append(OperationType::ERASE, key, "");记录操作
// 			   						 -> skiplist_.put(key, TableValue{ValueType::kTypeDeletion, ""});
// 			   						 -> 向skiplist中进行特殊put操作，值类型标记为墓碑值
// 			   -> get(K,V)读取一条数据  -> 先从内存的skiplis找,命中墓碑就返回false，说明数据被删除了
// 			   						->  从磁盘中找，通过维护的reader数组查询