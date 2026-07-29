
# LightKV: High-Performance Sharded LSM-Tree Key-Value Engine

A lightweight, concurrent, high-throughput Key-Value storage engine written in C++17. Designed around the LSM-Tree (Log-Structured Merge-tree) architecture, LightKV features multi-shard architecture, zero-copy memory views, background major compaction, non-blocking lock-free merges, and full crash-safe WAL persistence.

Under sustained high-concurrency workloads, LightKV delivers **80,000+ QPS** with microsecond-level latency and stable disk space consumption.

##  Key Features & Architecture

```text
                  ┌─────────────────────────────────────────────────┐
                  │              ShardedKVEngine                    │
                  │   (Global Background Compaction Scheduler)      │
                  └────────┬───────────────────────┬────────────────┘
                           │                       │
              ┌────────────▼──────────┐   ┌────────▼──────────────┐
              │   KVEngine (Shard 0)  │   │   KVEngine (Shard N)  │
              └────────────┬──────────┘   └────────┬──────────────┘
                           │                       │
          ┌────────────────┴─────────────┐         │
          │                              │         │
┌─────────▼────────┐           ┌─────────▼────────▼─┐
│ MemTable         │           │  WAL (Write-Ahead  │
│ (SkipList)       │           │        Log)        │
└─────────┬────────┘           └────────────────────┘
          │ (Auto-Flush)
┌─────────▼────────┐
│  SSTable Level   │ (Multi-way Merging & Non-blocking Compaction)
└──────────────────┘
```

- **Sharded Architecture**: Horizontal partition scaling via `ShardedKVEngine` to eliminate lock contention across threads.
- **LSM-Tree Core**:
  - **MemTable**: Custom thread-safe SkipList with dynamic level generation using fast modern PRNG engines (`std::mt19937`).
  - **SSTable & Block Indexing**: Compact binary format with dedicated Data Blocks, Index Blocks, and fixed 24-byte Footers for fast binary search retrievals.
  - **Write-Ahead Logging (WAL)**: Binary frame format featuring CRC32 data integrity verification, log rotation, and crash-safe automatic recovery.
- **Advanced Lock Separation (Lock-Free Merge)**:
  - Compaction splits locking overhead: Read-Lock Snapshot → Lock-Free Multi-way Merge & IO Write → Microsecond Unique-Lock Replacement.
  - Zero read/write stalls during prolonged disk rewrites.
- **Global Resource Control**:
  - Single-threaded global background scheduler managing multi-shard compaction.
  - Prevents thread explosions and avoids IO storms (IO spikes) during heavy traffic.
- **Zero-Copy Memory Abstraction**: Custom C++ `Slice` (`string_view`) primitives avoiding unnecessary heap allocations and supporting binary-safe keys.
- **GC & Tombstone Deletion**: Append-only deletions via tombstone flags, cleaned up atomically during Major Compaction.

##  Performance Benchmarks

- **Environment**: Huawei Cloud ECS Instance (8 vCPUs, Linux x86_64).
- **Test Duration**: 10-Minute High-Concurrency Long-Term Soak Test (`kv_soak`).

### Results Overview

| Metric | Measured Value |
| :--- | :--- |
| Sustained Write QPS | ~78,000 - 81,000 / sec |
| Sustained Read QPS | ~78,000 - 81,000 / sec |
| Cumulative Operations (10 Min) | ~96,000,000 Read/Write Ops |
| Disk Space Footprint | Stable at 6.7 GB – 7.2 GB (No Spikes/Leaks) |
| QPS Variance | < 3% (Zero Write Stalls) |

##  Build & Installation

### Prerequisites
- **Compiler**: GCC 9.0+ or Clang 10.0+ (C++17 support required)
- **Build System**: CMake 3.15+
- **OS**: Linux / macOS

### Quick Start

```bash
# Clone the repository
git clone git@github.com:your-username/cpp-kvstore.git
cd cpp-kvstore

# Build the project
mkdir build && cd build
cmake ..
make -j$(nproc)
```

##  API & Usage Example

```cpp
#include "sharded_kv_engine.h"
#include <iostream>
#include <cassert>

int main() {
    // Initialize engine with 16 shards storing data in "./data_dir"
    ShardedKVEngine db(16, "./data_dir");

    // 1. Put
    db.Put("user:1001", "Alice");
    db.Put("user:1002", "Bob");

    // 2. Get
    std::string value;
    if (db.Get("user:1001", &value)) {
        std::cout << "Found user: " << value << std::endl; // Alice
    }

    // 3. Delete (Append Tombstone)
    db.Delete("user:1002");

    // Verification
    bool exists = db.Get("user:1002", &value);
    assert(!exists); // Returns false

    return 0;
}
```

##  Running Benchmarks & Tests

### Integration & Unit Tests
To run all functional and integration tests (including crash recovery simulation and LSM-Tree lifecycle tests):

```bash
cd build
ctest --output-on-failure
```

### Soak & Load Testing
Run a 10-minute high-load stress test:

```bash
cd build
./kv_soak 10
```

##  Technical Highlights & Optimization Journey

- **Lock Separation in Compaction**:
  - **Problem**: Holding exclusive locks during Compaction causes severe IO stalls (dropping QPS by up to 70%).
  - **Solution**: Captured SSTable pointer snapshots under a shared read lock, performed multi-way merge and disk writing out-of-lock, and executed microsecond pointer swapping under an exclusive write lock.
- **Global IO Flow Control**:
  - **Problem**: Independent thread compaction per shard caused thread explosion (16+ threads) and IO storms.
  - **Solution**: Implemented a centralized background thread scheduler in `ShardedKVEngine` that streams compaction tasks sequentially based on SSTable count thresholds.
- **Exact File Lifecycle & Handle Management**:
  - **Problem**: Calculating file paths dynamically during deletion failed to remove old `.sst` physical files, creating unlinked ghost handles that inflated disk usage to 30GB+.
  - **Solution**: Bound immutable file paths directly to `SSTableReader` instances, ensuring accurate physical disk unlinking upon compaction.

##  License

Distributed under the MIT License.
