#include <benchmark/benchmark.h>
#include "kv_engine.h"

#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// 辅助函数：格式化 key
inline std::string MakeKey(size_t i) {
    char buf[32];
    snprintf(buf, sizeof(buf), "key_%010zu", i);
    return std::string(buf);
}

// 辅助函数：清理测试目录
void Cleanup(const std::string& path) {
    if (fs::exists(path)) {
        fs::remove_all(path);
    }
}

} // namespace

// -------------------------------------------------------------------
// 1. 顺序写性能基准测试 (Sequential Write)
// -------------------------------------------------------------------
static void BM_SequentialWrite(benchmark::State& state) {
    const std::string db_path = "./bench_google_seq_write_db";
    Cleanup(db_path);

    const size_t val_size = state.range(0); // 允许动态测试不同 Value 大小
    const std::string val_payload(val_size, 'v');

    {
        KVEngine engine(db_path, 1000); // threshold 设为 1000 触发正常 Flush
        size_t i = 0;

        for (auto _ : state) {
            std::string key = MakeKey(i++);
            
            // 精准测量的写入操作
            engine.put(key, val_payload);
        }
    }

    // 设置吞吐量与统计指标
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * (20 + val_size));

    Cleanup(db_path);
}

// -------------------------------------------------------------------
// 2. 随机写性能基准测试 (Random Write - 触发频繁 Flush 与 Compaction)
// -------------------------------------------------------------------
static void BM_RandomWrite(benchmark::State& state) {
    const std::string db_path = "./bench_google_rand_write_db";
    Cleanup(db_path);

    const size_t val_size = state.range(0);
    const std::string val_payload(val_size, 'v');
    const size_t max_keys = 200000; // 随机 Key池

    std::mt19937 g(1337);
    std::uniform_int_distribution<size_t> dist(0, max_keys - 1);

    {
        KVEngine engine(db_path, 1000);

        for (auto _ : state) {
            // 暂停计时：扣除随机数生成与字符串格式化的开销
            state.PauseTiming();
            std::string key = MakeKey(dist(g));
            state.ResumeTiming();

            // 精准测量 KV 写入
            engine.put(key, val_payload);
        }
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * (20 + val_size));

    Cleanup(db_path);
}

// -------------------------------------------------------------------
// 3. 随机读性能基准测试 (Random Read - 跨 SSTable / MemTable 点查)
// -------------------------------------------------------------------
static void BM_RandomRead(benchmark::State& state) {
    const std::string db_path = "./bench_google_rand_read_db";
    Cleanup(db_path);

    const size_t num_records = 30000;
    const size_t val_size = state.range(0);
    const std::string val_payload(val_size, 'v');

    // Setup 阶段：预填充数据并做 Major Compaction 模拟真实磁盘数据分布
    {
        KVEngine engine(db_path, 1000);
        for (size_t i = 0; i < num_records; ++i) {
            engine.put(MakeKey(i), val_payload);
        }
        engine.Compact();
    }

    // 开始测量读性能
    KVEngine engine(db_path, 1000);
    std::mt19937 g(42);
    std::uniform_int_distribution<size_t> dist(0, num_records - 1);

    for (auto _ : state) {
        state.PauseTiming();
        std::string key = MakeKey(dist(g));
        std::string value;
        state.ResumeTiming();

        bool found = engine.get(key, value);

        // 核心亮点：使用 DoNotOptimize 防止编译器把没被后续使用的 get/value 彻底优化剔除！
        benchmark::DoNotOptimize(found);
        benchmark::DoNotOptimize(value);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * (20 + val_size));

    Cleanup(db_path);
}

// ===================================================================
// 4. 多线程随机读压测 (Multi-Threaded Random Read)
// ===================================================================

// 定义一个 Fixture 类来管理跨线程共享的 KVEngine 实例
class KVEngineMTFixture : public benchmark::Fixture {
public:
    KVEngine* engine = nullptr;
    const std::string db_path = "./bench_google_mt_read_db";
    const size_t num_records = 30000;
    std::string val_payload;

    // SetUp 会在每个线程开始前调用，但我们只让 0 号线程执行初始化
    void SetUp(const ::benchmark::State& state) override {
        // state.range(0) 是通过 Arg() 传入的 value 大小
        val_payload = std::string(state.range(0), 'v');

        if (state.thread_index() == 0) {
            Cleanup(db_path);
            engine = new KVEngine(db_path, 1000);
            
            // 预热数据并压缩，模拟真实稳态
            for (size_t i = 0; i < num_records; ++i) {
                engine->put(MakeKey(i), val_payload);
            }
            engine->Compact();
        }
        // Google Benchmark 内部带有 Barrier，所有线程会在这里等待 0 号线程完成 SetUp
    }

    // TearDown 负责清理
    void TearDown(const ::benchmark::State& state) override {
        if (state.thread_index() == 0) {
            delete engine;
            engine = nullptr;
            Cleanup(db_path);
        }
    }
};

// 使用 BENCHMARK_DEFINE_F 宏定义多线程测试体
BENCHMARK_DEFINE_F(KVEngineMTFixture, RandomReadMT)(benchmark::State& state) {
    // 关键：每个线程必须有自己独立的随机数生成器！
    // 加上 state.thread_index() 作为种子偏移，保证各个线程查的 Key 不完全重合
    std::mt19937 g(42 + state.thread_index()); 
    std::uniform_int_distribution<size_t> dist(0, num_records - 1);

    for (auto _ : state) {
        state.PauseTiming();
        std::string key = MakeKey(dist(g));
        std::string value;
        state.ResumeTiming();

        // 并发调用 get (测试你的全局锁竞争)
        bool found = engine->get(key, value);

        benchmark::DoNotOptimize(found);
        benchmark::DoNotOptimize(value);
    }

    // 在多线程模式下，每个线程记录自己处理的量，Benchmark 最终会自动汇总求和
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * (20 + state.range(0)));
}


// 注册测试用例：测试 Value 为 128 字节和 1024 字节（1KB）的情况
BENCHMARK(BM_SequentialWrite)->Arg(128)->Arg(1024);
BENCHMARK(BM_RandomWrite)->Arg(128)->Arg(1024);
BENCHMARK(BM_RandomRead)->Arg(128)->Arg(1024);

// 注册多线程基准测试
// ThreadRange(1, 8) 会自动跑 1线程, 2线程, 4线程, 8线程 的情况
BENCHMARK_REGISTER_F(KVEngineMTFixture, RandomReadMT)
    ->ThreadRange(1, 16)
    ->Arg(1024)   // 只测 1KB 的 Value 即可，看趋势
    ->UseRealTime(); // 多线程下强烈建议使用墙上时钟(Real Time)而不是 CPU Time

    
// 自动生成 main 函数
BENCHMARK_MAIN();