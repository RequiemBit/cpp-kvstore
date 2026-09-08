#include <iostream>
#include <chrono>
#include <vector>
#include <string>
#include <iomanip>
#include <thread>
#include <atomic>
#include <filesystem>
#include <algorithm>
#include <mutex>

#include "sharded_kv_engine.h"

// 打印更宽的表格以容纳读写延迟
void PrintDualTableDivider() {
    std::cout << "+----------+-------------------+-----------------+-----------------+-----------------+-----------------+-----------------+-----------------+-----------------+-----------------+" << std::endl;
}

void TestAutonomousDrivingDualLatencySoak() {
    std::cout << "\n=================================================================================================================================================" << std::endl;
    std::cout << " [车载 KV 引擎 - 双向延迟与吞吐长效压测：写入 QPS + 20Hz视窗拉取 + 读/写双向 P50/P95/P99/P99.9 延迟统计]" << std::endl;
    std::cout << "=================================================================================================================================================" << std::endl;

    std::string test_dir = "./autonomous_dual_latency_db";
    if (std::filesystem::exists(test_dir)) {
        std::filesystem::remove_all(test_dir);
    }

    size_t shard_num = 16;
    ShardedKVEngine engine(shard_num, test_dir);

    const size_t PAYLOAD_SIZE = 256; 
    const int TOTAL_MINUTES = 10;
    const int NUM_WRITE_THREADS = 6;
    const int NUM_READ_THREADS = 2;

    std::atomic<bool> stop_flag{false};
    std::atomic<uint64_t> global_writes{0};
    std::atomic<uint64_t> global_scans{0};

    // 线程安全的延迟样本池（分别隔离写延迟与读延迟）
    std::mutex sample_mutex;
    std::vector<uint64_t> current_window_put_latencies;
    std::vector<uint64_t> current_window_get_latencies;
    current_window_put_latencies.reserve(500000);
    current_window_get_latencies.reserve(50000);

    // 1. 启动写入线程（优化 Key 策略：引入空间伪随机扰动，模拟车载点云网格，消除 SkipList 尾部挤压）
    std::vector<std::thread> write_threads;
    for (int t = 0; t < NUM_WRITE_THREADS; ++t) {
        write_threads.emplace_back([&engine, t, PAYLOAD_SIZE, &stop_flag, &global_writes, &current_window_put_latencies, &sample_mutex]() {
            uint64_t counter = 0;
            std::string payload(PAYLOAD_SIZE, 'v');
            while (!stop_flag.load()) {
                // 🌟 核心改进：用 3D 空间空间网格扰动代替纯线性自增，避免跳表尾部 CPU 缓存行颠簸
                uint64_t x = (counter * 73856093) ^ (t * 19349663);
                uint64_t y = (counter * 19349663) ^ (t * 83492791);
                uint64_t z = (counter * 83492791) ^ (t * 73856093);
                
                std::string key = "voxel_t" + std::to_string(t) + "_" + 
                                  std::to_string(x % 100000) + "_" + 
                                  std::to_string(y % 100000) + "_" + 
                                  std::to_string(z % 1000);
                counter++;
                
                auto start = std::chrono::high_resolution_clock::now();
                engine.Put(key, payload);
                auto end = std::chrono::high_resolution_clock::now();
                
                uint64_t lat_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                global_writes.fetch_add(1, std::memory_order_relaxed);

                if (counter % 20 == 0) {
                    std::lock_guard<std::mutex> lock(sample_mutex);
                    current_window_put_latencies.push_back(lat_us);
                }
            }
        });
    }

    // 2. 启动 20Hz 规划闭环读取线程（点查询 Get + 多通道均摊 + 动态热点跟随）
    std::vector<std::thread> read_threads;
    for (int t = 0; t < NUM_READ_THREADS; ++t) {
        read_threads.emplace_back([&engine, &stop_flag, &global_scans, &global_writes, &current_window_get_latencies, &sample_mutex]() {
            const size_t SCAN_WINDOW_SIZE = 10000; // 维持 1 万次 Get 负荷不变
            const auto target_interval = std::chrono::milliseconds(50); // 维持 20 Hz 节奏不变
            uint64_t get_counter = 0;

            while (!stop_flag.load()) {
                auto loop_start = std::chrono::steady_clock::now();
                
                size_t found = 0;
                uint64_t current_total = global_writes.load();
                uint64_t approx_per_writer = current_total / 6;
                uint64_t max_bound = std::max(static_cast<uint64_t>(50000), approx_per_writer);

                for (size_t i = 0; i < SCAN_WINDOW_SIZE; ++i) {
                    int writer_id = i % 6;
                    
                    // 动态跟随写入端生成的空间网格 Key 规律进行点查询
                    uint64_t simulated_counter = (i + get_counter) % max_bound;
                    uint64_t x = (simulated_counter * 73856093) ^ (writer_id * 19349663);
                    uint64_t y = (simulated_counter * 19349663) ^ (writer_id * 83492791);
                    uint64_t z = (simulated_counter * 83492791) ^ (writer_id * 73856093);

                    std::string key = "voxel_t" + std::to_string(writer_id) + "_" + 
                                      std::to_string(x % 100000) + "_" + 
                                      std::to_string(y % 100000) + "_" + 
                                      std::to_string(z % 1000);
                    std::string val;

                    // 对视窗内的 Get 操作进行微秒级耗时采样
                    auto get_start = std::chrono::high_resolution_clock::now();
                    bool success = engine.Get(key, val);
                    auto get_end = std::chrono::high_resolution_clock::now();

                    if (success) {
                        found++;
                    }

                    // 抽样视窗内的单次 Get 延迟（每 50 次 Get 抽样 1 次）
                    if ((++get_counter) % 50 == 0) {
                        uint64_t get_lat_us = std::chrono::duration_cast<std::chrono::microseconds>(get_end - get_start).count();
                        std::lock_guard<std::mutex> lock(sample_mutex);
                        current_window_get_latencies.push_back(get_lat_us);
                    }
                }
                global_scans.fetch_add(1, std::memory_order_relaxed);

                // 锁死 20Hz 节奏
                auto loop_end = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(loop_end - loop_start);
                if (elapsed < target_interval) {
                    std::this_thread::sleep_for(target_interval - elapsed);
                }
            }
        });
    }

    // 3. 统计主循环
    std::cout << "\n[双向压测进行中] 正在收集写延迟与读（Get）延迟双向指标..." << std::endl;
    PrintDualTableDivider();
    std::cout << "| 阶段 (Min) | 阶段写入 QPS | 写 P99 (us) | 写 P99.9(us) | 视窗拉取次数 | 读 P99 (us) | 读 P99.9(us) |" << std::endl;
    PrintDualTableDivider();

    uint64_t last_writes = 0;

    for (int minute = 1; minute <= TOTAL_MINUTES; ++minute) {
        auto window_start_time = std::chrono::steady_clock::now();
        
        // 清空双向延迟样本池
        {
            std::lock_guard<std::mutex> lock(sample_mutex);
            current_window_put_latencies.clear();
            current_window_get_latencies.clear();
        }

        std::this_thread::sleep_for(std::chrono::seconds(60));

        auto window_end_time = std::chrono::steady_clock::now();
        double actual_seconds = std::chrono::duration<double>(window_end_time - window_start_time).count();

        uint64_t current_total_writes = global_writes.load();
        uint64_t window_writes = current_total_writes - last_writes;
        last_writes = current_total_writes;

        uint64_t current_total_scans = global_scans.load();
        double qps = static_cast<double>(window_writes) / actual_seconds;

        // 拷贝并计算写入延迟百分位
        std::vector<uint64_t> put_samples_copy;
        std::vector<uint64_t> get_samples_copy;
        {
            std::lock_guard<std::mutex> lock(sample_mutex);
            put_samples_copy = current_window_put_latencies;
            get_samples_copy = current_window_get_latencies;
        }

        uint64_t put_p99 = 0, put_p999 = 0;
        if (!put_samples_copy.empty()) {
            std::sort(put_samples_copy.begin(), put_samples_copy.end());
            put_p99  = put_samples_copy[put_samples_copy.size() * 99 / 100];
            put_p999 = put_samples_copy[put_samples_copy.size() * 999 / 1000];
        }

        uint64_t get_p99 = 0, get_p999 = 0;
        if (!get_samples_copy.empty()) {
            std::sort(get_samples_copy.begin(), get_samples_copy.end());
            get_p99  = get_samples_copy[get_samples_copy.size() * 99 / 100];
            get_p999 = get_samples_copy[get_samples_copy.size() * 999 / 1000];
        }

        // 输出精简且重点突出的双向延迟与 QPS 对比行
        std::cout << "| " << std::setw(10) << ("M " + std::to_string(minute)) 
                  << " | " << std::setw(13) << static_cast<uint64_t>(qps)
                  << " | " << std::setw(11) << put_p99 
                  << " | " << std::setw(12) << put_p999 
                  << " | " << std::setw(12) << current_total_scans 
                  << " | " << std::setw(11) << get_p99 
                  << " | " << std::setw(12) << get_p999 << " |" << std::endl;
    }

    PrintDualTableDivider();

    stop_flag.store(true);
    for (auto& th : write_threads) th.join();
    for (auto& th : read_threads) th.join();

    std::cout << "\n🎉 双向延迟长效压测圆满完成！" << std::endl;
}

int main() {
    TestAutonomousDrivingDualLatencySoak();
    return 0;
}