#ifndef SPATIAL_KEY_H
#define SPATIAL_KEY_H

#include <cstdint>
#include <string>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include <endian.h> // 用于大端序转换，确保底层 memcmp 字典序与数值大小完全一致
#include "slice.h"

// 强制 1 字节对齐，确保内存紧凑无 padding
#pragma pack(push, 1)
struct SpatialKey3D {
    uint8_t  layer_type;   // 1B: 图层类型 (0x01: Occupancy Grid, 0x02: LiDAR PointCloud)
    uint8_t  lod_level;    // 1B: 金字塔 LOD 层级 (0: 0.1m 极高精, 1: 0.5m 局部网格)
    uint64_t morton_code;  // 8B: 3D Morton Code (Z-Order 曲线编码)
    uint64_t timestamp_us; // 8B: 微秒级时间戳 (支撑时序回溯与覆盖)

    // 将多字节整型转为网络大端序，确保 LSM-Tree 底层 memcmp 排序正确
    std::string ToBytes() const {
        SpatialKey3D net_key;
        net_key.layer_type = layer_type;
        net_key.lod_level = lod_level;
        net_key.morton_code = htobe64(morton_code);
        net_key.timestamp_us = htobe64(timestamp_us);

        std::string buf;
        buf.resize(sizeof(SpatialKey3D));
        std::memcpy(&buf[0], &net_key, sizeof(SpatialKey3D));
        return buf;
    }

    // 从底层 Slice 反序列化还原 Key（大端序转回主机序）
    static SpatialKey3D FromSlice(const Slice& s) {
        SpatialKey3D net_key;
        std::memcpy(&net_key, s.data(), sizeof(SpatialKey3D));

        SpatialKey3D host_key;
        host_key.layer_type = net_key.layer_type;
        host_key.lod_level = net_key.lod_level;
        host_key.morton_code = be64toh(net_key.morton_code);
        host_key.timestamp_us = be64toh(net_key.timestamp_us);
        return host_key;
    }

    Slice ToSlice() const {
        return Slice(reinterpret_cast<const char*>(this), sizeof(SpatialKey3D));
    }
};
#pragma pack(pop)

static_assert(sizeof(SpatialKey3D) == 18, "SpatialKey3D size must be exactly 18 bytes");

class Morton3D {
public:
    static uint64_t SplitBy2(uint64_t a) {
        a &= 0x1fffff; 
        a = (a | (a << 32)) & 0x1f00000000ffffULL;
        a = (a | (a << 16)) & 0x1f0000ff0000ffULL;
        a = (a | (a << 8))  & 0x100f00f00f00f00fULL;
        a = (a | (a << 4))  & 0x10c30c30c30c30c3ULL;
        a = (a | (a << 2))  & 0x1249249249249249ULL;
        return a;
    }

    // 单点编码
    static uint64_t Encode(double x, double y, double z, double resolution = 0.1) {
        uint64_t ix = static_cast<uint64_t>(std::round((x + 100000.0) / resolution));
        uint64_t iy = static_cast<uint64_t>(std::round((y + 100000.0) / resolution));
        uint64_t iz = static_cast<uint64_t>(std::round((z + 100000.0) / resolution));

        return (SplitBy2(ix) << 2) | (SplitBy2(iy) << 1) | SplitBy2(iz);
    }

    // 单点解码
    static void Decode(uint64_t code, double& x, double& y, double& z, double resolution = 0.1) {
        uint32_t ix = 0, iy = 0, iz = 0;
        for (int i = 0; i < 21; ++i) {
            ix |= ((code >> (3 * i + 0)) & 1ULL) << i;
            iy |= ((code >> (3 * i + 1)) & 1ULL) << i;
            iz |= ((code >> (3 * i + 2)) & 1ULL) << i;
        }

        x = static_cast<double>(ix) * resolution - 100000.0;
        y = static_cast<double>(iy) * resolution - 100000.0;
        z = static_cast<double>(iz) * resolution - 100000.0;
    }
};

struct MortonRange {
    uint64_t start_code;
    uint64_t end_code;
};

class MortonRangeGenerator {
public:
    static std::vector<MortonRange> GenerateRanges(
        double x_min, double y_min, double z_min,
        double x_max, double y_max, double z_max,
        double resolution = 0.1) 
    {
        uint64_t ix_min = static_cast<uint64_t>(std::round((x_min + 100000.0) / resolution));
        uint64_t iy_min = static_cast<uint64_t>(std::round((y_min + 100000.0) / resolution));
        uint64_t iz_min = static_cast<uint64_t>(std::round((z_min + 100000.0) / resolution));

        uint64_t ix_max = static_cast<uint64_t>(std::round((x_max + 100000.0) / resolution));
        uint64_t iy_max = static_cast<uint64_t>(std::round((y_max + 100000.0) / resolution));
        uint64_t iz_max = static_cast<uint64_t>(std::round((z_max + 100000.0) / resolution));

        if (ix_min > ix_max) std::swap(ix_min, ix_max);
        if (iy_min > iy_max) std::swap(iy_min, iy_max);
        if (iz_min > iz_max) std::swap(iz_min, iz_max);

        std::vector<MortonRange> ranges;

        // 使用显式栈（Heap Stack）代替递归函数调用，彻底避免栈溢出（Stack Overflow）
        struct BoxNode {
            uint64_t x1, y1, z1;
            uint64_t x2, y2, z2;
        };

        std::vector<BoxNode> stack;
        stack.push_back({ix_min, iy_min, iz_min, ix_max, iy_max, iz_max});
        stack.reserve(64);

        while (!stack.empty()) {
            BoxNode node = stack.back();
            stack.pop_back();

            if (node.x1 > node.x2 || node.y1 > node.y2 || node.z1 > node.z2) continue;

            uint64_t dx = node.x2 - node.x1;
            uint64_t dy = node.y2 - node.y1;
            uint64_t dz = node.z2 - node.z1;

            // 优化策略：当子区域足够小（体素数 < 64）时，不再继续分裂，
            // 直接生成该小方块的边界 Morton 码区间，大幅提高性能并减少区间碎片
            if ((dx * dy * dz < 64) || (dx == 0 && dy == 0 && dz == 0)) {
                uint64_t min_code = (Morton3D::SplitBy2(node.x1) << 2) | (Morton3D::SplitBy2(node.y1) << 1) | Morton3D::SplitBy2(node.z1);
                uint64_t max_code = (Morton3D::SplitBy2(node.x2) << 2) | (Morton3D::SplitBy2(node.y2) << 1) | Morton3D::SplitBy2(node.z2);
                ranges.push_back({min_code, max_code});
                continue;
            }

            // 三维中点对半切分（八叉树 8 分割）
            uint64_t mx = node.x1 + (dx / 2);
            uint64_t my = node.y1 + (dy / 2);
            uint64_t mz = node.z1 + (dz / 2);

            // 压入 8 个子象限到显式堆栈中
            stack.push_back({node.x1, node.y1, node.z1, mx, my, mz});
            if (mx < node.x2) stack.push_back({mx + 1, node.y1, node.z1, node.x2, my, mz});
            if (my < node.y2) stack.push_back({node.x1, my + 1, node.z1, mx, node.y2, mz});
            if (mx < node.x2 && my < node.y2) stack.push_back({mx + 1, my + 1, node.z1, node.x2, node.y2, mz});
            if (mz < node.z2) stack.push_back({node.x1, node.y1, mz + 1, mx, my, node.z2});
            if (mx < node.x2 && mz < node.z2) stack.push_back({mx + 1, node.y1, mz + 1, node.x2, my, node.z2});
            if (my < node.y2 && mz < node.z2) stack.push_back({node.x1, my + 1, mz + 1, mx, node.y2, node.z2});
            if (mx < node.x2 && my < node.y2 && mz < node.z2) stack.push_back({mx + 1, my + 1, mz + 1, node.x2, node.y2, node.z2});
        }

        // 对生成的零散区间进行合并与排序
        return MergeRanges(ranges);
    }

private:
    // 辅助函数：将区间排序并合并相邻/相交区间
    static std::vector<MortonRange> MergeRanges(std::vector<MortonRange>& raw_ranges) {
        if (raw_ranges.empty()) return {};

        std::sort(raw_ranges.begin(), raw_ranges.end(), [](const MortonRange& a, const MortonRange& b) {
            return a.start_code < b.start_code;
        });

        std::vector<MortonRange> merged;
        MortonRange current = raw_ranges[0];

        for (size_t i = 1; i < raw_ranges.size(); ++i) {
            if (raw_ranges[i].start_code <= current.end_code + 1) {
                current.end_code = std::max(current.end_code, raw_ranges[i].end_code);
            } else {
                merged.push_back(current);
                current = raw_ranges[i];
            }
        }
        merged.push_back(current);
        return merged;
    }
};

#endif // SPATIAL_KEY_H