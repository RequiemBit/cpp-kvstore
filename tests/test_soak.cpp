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

void PrintLog(const std::string& msg) {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::cout << "[" << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << "] " << msg << std::endl;
}

int main(int argc, char* argv[]) {
    // 默认测试时长：10 分钟（可通过命令行参数传入，如 ./kv_soak 60 表示测试 60 分钟）
    int test_duration_minutes = (argc > 1) ? std::stoi(argv[1]) : 10;

    PrintLog("开始 ShardedKVEngine 长时间稳定性压测 (Soak Test)...");
    PrintLog("计划运行时长: " + std::to_string(test_duration_minutes) + " 分钟");

    std::string test_dir = "./soak_test_data";
    if (std::filesystem::exists(test_dir)) {
        std::filesystem::remove_all(test_dir);
    }

    size_t shard_num = 16;
    ShardedKVEngine engine(shard_num, test_dir);

    const size_t NUM_THREADS = 8;
    const size_t RECORD_SIZE = 1024; // 1KB Payload
    std::atomic<bool> running{true};
    std::atomic<uint64_t> total_writes{0};
    std::atomic<uint64_t> total_reads{0};
    std::atomic<uint64_t> read_success{0};

    // 启动 8 个混合读写线程
    std::vector<std::thread> threads;
    for (size_t t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&engine, t, RECORD_SIZE, &running, &total_writes, &total_reads, &read_success]() {
            std::string payload(RECORD_SIZE, 's');
            std::mt19937 gen(1337 + t);
            std::uniform_int_distribution<uint64_t> dist(1, 1000000); // 在 100 万 Key 空间内随机读写

            while (running.load()) {
                uint64_t id = dist(gen);
                std::string key = "soak_key_" + std::to_string(id);

                // 50% 概率写入，50% 概率读取
                if (id % 2 == 0) {
                    engine.Put(key, payload);
                    total_writes++;
                } else {
                    std::string val;
                    if (engine.Get(key, &val)) {
                        read_success++;
                    }
                    total_reads++;
                }
            }
        });
    }

    // 主线程：定期监控与日志打印
    auto start_time = std::chrono::steady_clock::now();
    auto last_time = start_time;
    uint64_t last_writes = 0;
    uint64_t last_reads = 0;

    int elapsed_minutes = 0;
    while (elapsed_minutes < test_duration_minutes) {
        std::this_thread::sleep_for(std::chrono::minutes(1)); // 每 1 分钟打印一次状态
        elapsed_minutes++;

        auto current_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> interval_dur = current_time - last_time;

        uint64_t curr_writes = total_writes.load();
        uint64_t curr_reads = total_reads.load();

        uint64_t interval_writes = curr_writes - last_writes;
        uint64_t interval_reads = curr_reads - last_reads;

        double write_qps = interval_writes / interval_dur.count();
        double read_qps = interval_reads / interval_dur.count();

        PrintLog("运行时间: " + std::to_string(elapsed_minutes) + "/" + std::to_string(test_duration_minutes) + " 分钟 | "
                 + "近1分钟写 QPS: " + std::to_string(static_cast<uint64_t>(write_qps)) + " | "
                 + "近1分钟读 QPS: " + std::to_string(static_cast<uint64_t>(read_qps)) + " | "
                 + "累计写入: " + std::to_string(curr_writes) + " | "
                 + "累计读取: " + std::to_string(curr_reads));

        last_time = current_time;
        last_writes = curr_writes;
        last_reads = curr_reads;
    }

    // 通知所有子线程退出
    running.store(false);
    for (auto& th : threads) {
        th.join();
    }

    PrintLog("==========================================");
    PrintLog("长时间稳定性测试完成！无崩溃，系统运行稳定。");
    PrintLog("总写入次数: " + std::to_string(total_writes.load()));
    PrintLog("总读取次数: " + std::to_string(total_reads.load()));
    PrintLog("==========================================");

    return 0;
}