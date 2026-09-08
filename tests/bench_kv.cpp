#include <benchmark/benchmark.h>
#include "sharded_kv_engine.h"

#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

    // 辅助函数：格式化 key（保持与原测试一致）
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

    // 默认分片数设为 16
    constexpr size_t kShardNum = 16;
    // 默认每个 Shard 的 MemTable 阈值（原 threshold 1000）
    constexpr size_t kMemTableThreshold = 1000;

} // namespace

// -------------------------------------------------------------------
// 1. 顺序写性能基准测试 (Sequential Write - Sharded)
// -------------------------------------------------------------------
static void BM_SequentialWrite(benchmark::State& state) {
    const std::string db_path = "./bench_sharded_seq_write_db";
    Cleanup(db_path);

    const size_t val_size = state.range(0);
    const std::string val_payload(val_size, 'v');

    {
        // 实例化分片引擎，物理路径会自动隔离为 db_path/shard_0 ~ shard_15
        ShardedKVEngine engine(kShardNum, db_path);
        size_t i = 0;

        for (auto _ : state) {
            std::string key = MakeKey(i++);
            engine.Put(key, val_payload);
        }
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * (20 + val_size));

    Cleanup(db_path);
}

// -------------------------------------------------------------------
// 2. 随机写性能基准测试 (Random Write - Sharded)
// -------------------------------------------------------------------
static void BM_RandomWrite(benchmark::State& state) {
    const std::string db_path = "./bench_sharded_rand_write_db";
    Cleanup(db_path);

    const size_t val_size = state.range(0);
    const std::string val_payload(val_size, 'v');
    const size_t max_keys = 200000;

    std::mt19937 g(1337);
    std::uniform_int_distribution<size_t> dist(0, max_keys - 1);

    {
        ShardedKVEngine engine(kShardNum, db_path);

        for (auto _ : state) {
            state.PauseTiming();
            std::string key = MakeKey(dist(g));
            state.ResumeTiming();

            engine.Put(key, val_payload);
        }
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * (20 + val_size));

    Cleanup(db_path);
}

// -------------------------------------------------------------------
// 3. 单线程随机读性能基准测试 (Random Read - Sharded)
// -------------------------------------------------------------------
static void BM_RandomRead(benchmark::State& state) {
    const std::string db_path = "./bench_sharded_rand_read_db";
    Cleanup(db_path);

    const size_t num_records = 30000;
    const size_t val_size = state.range(0);
    const std::string val_payload(val_size, 'v');

    // Setup 阶段：预填充数据
    {
        ShardedKVEngine engine(kShardNum, db_path);
        for (size_t i = 0; i < num_records; ++i) {
            engine.Put(MakeKey(i), val_payload);
        }
        // 注：Sharded 模式下由各自 Shard 内部维护 Flush/Compaction
    }

    // 开始测量读性能
    ShardedKVEngine engine(kShardNum, db_path);
    std::mt19937 g(42);
    std::uniform_int_distribution<size_t> dist(0, num_records - 1);

    for (auto _ : state) {
        state.PauseTiming();
        std::string key = MakeKey(dist(g));
        std::string value;
        state.ResumeTiming();

        bool found = engine.Get(key, &value);

        benchmark::DoNotOptimize(found);
        benchmark::DoNotOptimize(value);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * (20 + val_size));

    Cleanup(db_path);
}

// ===================================================================
// 4. 多线程随机读压测 (Multi-Threaded Random Read - Sharded)
// ===================================================================

class ShardedKVEngineMTFixture : public benchmark::Fixture {
public:
    ShardedKVEngine* engine = nullptr;
    const std::string db_path = "./bench_sharded_mt_read_db";
    const size_t num_records = 30000;
    std::string val_payload;

    void SetUp(const ::benchmark::State& state) override {
        val_payload = std::string(state.range(0), 'v');

        if (state.thread_index() == 0) {
            Cleanup(db_path);
            engine = new ShardedKVEngine(kShardNum, db_path);
            
            // 预热数据到 16 个 Shard 中
            for (size_t i = 0; i < num_records; ++i) {
                engine->Put(MakeKey(i), val_payload);
            }
        }
    }

    void TearDown(const ::benchmark::State& state) override {
        if (state.thread_index() == 0) {
            delete engine;
            engine = nullptr;
            Cleanup(db_path);
        }
    }
};

BENCHMARK_DEFINE_F(ShardedKVEngineMTFixture, RandomReadMT)(benchmark::State& state) {
    std::mt19937 g(42 + state.thread_index()); 
    std::uniform_int_distribution<size_t> dist(0, num_records - 1);

    for (auto _ : state) {
        state.PauseTiming();
        std::string key = MakeKey(dist(g));
        std::string value;
        state.ResumeTiming();

        // 顶层无锁路由 + Shard 内部读写锁并发 Get
        bool found = engine->Get(key, &value);

        benchmark::DoNotOptimize(found);
        benchmark::DoNotOptimize(value);
    }

    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * (20 + state.range(0)));
}

// -------------------------------------------------------------------
// 5. 注册测试用例（条件与原测试保持完全一致）
// -------------------------------------------------------------------

BENCHMARK(BM_SequentialWrite)->Arg(128)->Arg(1024);
BENCHMARK(BM_RandomWrite)->Arg(128)->Arg(1024);
BENCHMARK(BM_RandomRead)->Arg(128)->Arg(1024);

BENCHMARK_REGISTER_F(ShardedKVEngineMTFixture, RandomReadMT)
    ->ThreadRange(1, 16)
    ->Arg(1024)
    ->UseRealTime();

BENCHMARK_MAIN();