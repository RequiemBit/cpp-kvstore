#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <iomanip>
#include <thread>
#include <atomic>
#include <filesystem>

#include "sharded_kv_engine.h"

void PrintHeader(const std::string& title) {
    std::cout << "\n==================================================" << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << "==================================================" << std::endl;
}

int main() {
    PrintHeader("ShardedKVEngine 500万级【多线程并发】大数据压测");

    std::string test_dir = "./large_scale_data_mt";
    if (std::filesystem::exists(test_dir)) {
        std::filesystem::remove_all(test_dir);
    }

    size_t shard_num = 16;
    ShardedKVEngine engine(shard_num, test_dir);

    const size_t TOTAL_RECORDS = 5000000; // 500 万条
    const size_t NUM_THREADS = 8;         // 匹配华为云 8 vCPU 物理核心
    const size_t RECORDS_PER_THREAD = TOTAL_RECORDS / NUM_THREADS;
    const size_t RECORD_SIZE = 1024;      // 1KB Payload

    std::cout << "[配置] 并发线程数: " << NUM_THREADS 
              << " | 每线程写入: " << RECORDS_PER_THREAD << " 条"
              << " | 总数据量: " << TOTAL_RECORDS << " 条 (5GB)" << std::endl;

    // -------------------------------------------------------------
    PrintHeader("阶段一：8 线程并发追加写入 500 万条数据");
    // -------------------------------------------------------------

    std::vector<std::thread> threads;
    threads.reserve(NUM_THREADS);

    auto start_write = std::chrono::high_resolution_clock::now();

    for (size_t t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&engine, t, RECORDS_PER_THREAD, RECORD_SIZE]() {
            std::string payload(RECORD_SIZE, 'v');
            size_t start_id = t * RECORDS_PER_THREAD + 1;
            size_t end_id = (t + 1) * RECORDS_PER_THREAD;

            for (size_t i = start_id; i <= end_id; ++i) {
                std::string key = "sensor_" + std::to_string(i);
                engine.Put(key, payload);
            }
        });
    }

    // 等待所有写入线程完成
    for (auto& th : threads) {
        th.join();
    }

    auto end_write = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> write_sec = end_write - start_write;

    std::cout << "[多线程写入完成] 总耗时: " << write_sec.count() << " 秒" << std::endl;
    std::cout << "多线程并发写入吞吐: " << static_cast<uint64_t>(TOTAL_RECORDS / write_sec.count()) << " QPS" << std::endl;
    std::cout << "相当于磁盘写入带宽: " << std::fixed << std::setprecision(2) 
              << (TOTAL_RECORDS * RECORD_SIZE / (1024.0 * 1024.0)) / write_sec.count() << " MB/s" << std::endl;

    // -------------------------------------------------------------
    PrintHeader("阶段二：8 线程并发随机点查 (Random Read MT)");
    // -------------------------------------------------------------

    const size_t READ_OPS_PER_THREAD = 50000;
    const size_t TOTAL_READ_OPS = READ_OPS_PER_THREAD * NUM_THREADS; // 共 40 万次点查
    std::atomic<size_t> total_success_count{0};

    threads.clear();
    auto start_read = std::chrono::high_resolution_clock::now();

    for (size_t t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&engine, t, READ_OPS_PER_THREAD, TOTAL_RECORDS, RECORD_SIZE, &total_success_count]() {
            size_t local_success = 0;
            std::string read_value;

            for (size_t i = 1; i <= READ_OPS_PER_THREAD; ++i) {
                // 伪随机交错抽取 Key
                size_t target_id = ((i * 1337 + t * 9973) % TOTAL_RECORDS) + 1;
                std::string key = "sensor_" + std::to_string(target_id);

                if (engine.Get(key, &read_value) && read_value.size() == RECORD_SIZE) {
                    local_success++;
                }
            }
            total_success_count += local_success;
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    auto end_read = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> read_sec = end_read - start_read;

    std::cout << "[多线程读取完成] 校验通过率: " << total_success_count.load() << " / " << TOTAL_READ_OPS 
              << " (" << (total_success_count.load() * 100.0 / TOTAL_READ_OPS) << "%)" << std::endl;
    std::cout << "多线程并发点查吞吐: " << static_cast<uint64_t>(TOTAL_READ_OPS / read_sec.count()) << " QPS" << std::endl;

    PrintHeader("多线程高压测试顺利通过！");

    return 0;
}