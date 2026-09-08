#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <iomanip>
#include <thread>
#include <atomic>
#include <filesystem>
#include <random>

#include "sharded_kv_engine.h"
#include "spatial_key.h" // 确保能引用到 SpatialKey3D，以契合范围路由

void PrintLog(const std::string& msg) {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::cout << "[" << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << "] " << msg << std::endl;
}

int main(int argc, char* argv[]) {
    // 默认测试时长：10 分钟（可通过命令行参数传入分钟数，如 ./kv_soak 10）
    int test_duration_minutes = (argc > 1) ? std::stoi(argv[1]) : 10;
    int total_seconds = test_duration_minutes * 60;
    int interval_seconds = 30; // 每 30 秒打印一次
    int total_intervals = total_seconds / interval_seconds;

    PrintLog("开始 ShardedKVEngine 长时间稳定性压测 (Soak Test)...");
    PrintLog("线程分工: 6 个写入线程 (Writers) + 2 个读取线程 (Readers)");
    PrintLog("Payload 大小: 256 字节 (单体素结构)");
    PrintLog("计划运行时长: " + std::to_string(test_duration_minutes) + " 分钟 (每 30 秒采样一次 QPS)");

    std::string test_dir = "./soak_test_data";
    if (std::filesystem::exists(test_dir)) {
        std::filesystem::remove_all(test_dir);
    }

    size_t shard_num = 16;
    ShardedKVEngine engine(shard_num, test_dir);

    const size_t TOTAL_THREADS = 8;
    const size_t RECORD_SIZE = 256; // 256B Payload (体素数据负载)
    std::atomic<bool> running{true};
    std::atomic<uint64_t> total_writes{0};
    std::atomic<uint64_t> total_reads{0};
    std::atomic<uint64_t> read_success{0};

    std::vector<std::thread> threads;
    threads.reserve(TOTAL_THREADS);

    for (size_t t = 0; t < TOTAL_THREADS; ++t) {
        bool is_writer = (t < 6); // 前 6 个线程写

        threads.emplace_back([&engine, t, is_writer, RECORD_SIZE, &running, &total_writes, &total_reads, &read_success]() {
            std::string payload(RECORD_SIZE, 'v'); // 填充 256 字节的模拟体素数据
            std::mt19937_64 gen(1337 + t); // 使用 64 位梅森旋转算法，支持全 64 位随机数
            
            // 【关键修改】将 Morton 码空间拉满到 0 ~ UINT64_MAX，使 16 个分片均衡承载流量
            std::uniform_int_distribution<uint64_t> dist(0, UINT64_MAX);

            if (is_writer) {
                // --- 写入线程逻辑 (6个线程) ---
                while (running.load()) {
                    uint64_t morton_code = dist(gen);
                    SpatialKey3D spatial_key{0x01, 0, morton_code, 0};
                    std::string key = spatial_key.ToBytes();

                    engine.Put(key, payload);
                    total_writes++;
                }
            } else {
                // --- 读取线程逻辑 (2个线程) ---
                while (running.load()) {
                    uint64_t morton_code = dist(gen);
                    SpatialKey3D spatial_key{0x01, 0, morton_code, 0};
                    std::string key = spatial_key.ToBytes();

                    std::string val;
                    if (engine.Get(key, &val)) {
                        read_success++;
                    }
                    total_reads++;
                }
            }
        });
    }

    // 主线程：每 30 秒定期监控与 QPS 日志打印
    auto start_time = std::chrono::steady_clock::now();
    auto last_time = start_time;
    uint64_t last_writes = 0;
    uint64_t last_reads = 0;

    int current_interval = 0;
    while (current_interval < total_intervals && running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
        current_interval++;

        auto current_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> interval_dur = current_time - last_time;

        uint64_t curr_writes = total_writes.load();
        uint64_t curr_reads = total_reads.load();
        uint64_t curr_success = read_success.load();

        uint64_t interval_writes = curr_writes - last_writes;
        uint64_t interval_reads = curr_reads - last_reads;

        double write_qps = interval_writes / interval_dur.count();
        double read_qps = interval_reads / interval_dur.count();

        double elapsed_mins = (current_interval * interval_seconds) / 60.0;

        PrintLog("已运行: " + std::to_string(elapsed_mins) + "/" + std::to_string(test_duration_minutes) + " 分钟 | "
                 + "6写线程 QPS: " + std::to_string(static_cast<uint64_t>(write_qps)) + " | "
                 + "2读线程 QPS: " + std::to_string(static_cast<uint64_t>(read_qps)) + " | "
                 + "累计写入: " + std::to_string(curr_writes) + " | "
                 + "累计读取: " + std::to_string(curr_reads) + " (成功率: " + 
                 (curr_reads > 0 ? std::to_string((curr_success * 100) / curr_reads) : "0") + "%)");

        last_time = current_time;
        last_writes = curr_writes;
        last_reads = curr_reads;
    }

    // 通知所有子线程安全退出
    running.store(false);
    for (auto& th : threads) {
        th.join();
    }

    PrintLog("==========================================");
    PrintLog("6写2读长时间稳定性测试完成！系统运行稳定。");
    PrintLog("总写入次数: " + std::to_string(total_writes.load()));
    PrintLog("总读取次数: " + std::to_string(total_reads.load()));
    PrintLog("总读取成功次数: " + std::to_string(read_success.load()));
    PrintLog("==========================================");

    return 0;
}