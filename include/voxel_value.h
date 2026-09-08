#ifndef VOXEL_VALUE_H
#define VOXEL_VALUE_H

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include "slice.h"

// 单个点云/栅格在体素块内部的相对坐标量化表示 (仅占用 7 字节)
#pragma pack(push, 1)
struct CompressedPoint3D {
    int16_t delta_x;    // 相对体素中心 X 偏移 (毫米级 mm)
    int16_t delta_y;    // 相对体素中心 Y 偏移 (毫米级 mm)
    int16_t delta_z;    // 相对体素中心 Z 偏移 (毫米级 mm)
    uint8_t intensity;  // 反射强度 / 占据概率
};

// 体素块元数据 Header (固定 16 字节)
struct VoxelHeader {
    int32_t base_x_mm;     // 体素中心点世界坐标 X (毫米定点数)
    int32_t base_y_mm;     // 体素中心点世界坐标 Y (毫米定点数)
    int32_t base_z_mm;     // 体素中心点世界坐标 Z (毫米定点数)
    uint8_t occupancy_prob;// 占据概率 (0-100)
    uint8_t quality_score; // SLAM 建图置信度得分
    uint16_t point_count;  // 包含的压缩点数量 N
};
#pragma pack(pop)

// Value Builder & Parser 序列化类
class VoxelBlockValue {
public:
    // 将解压后的 3D 点数据序列化打包为二进制 std::string (用于写入 KV Engine)
    static std::string Serialize(
        int32_t base_x_mm, int32_t base_y_mm, int32_t base_z_mm,
        uint8_t occupancy_prob, uint8_t quality_score,
        const std::vector<CompressedPoint3D>& points) 
    {
        VoxelHeader header;
        header.base_x_mm = base_x_mm;
        header.base_y_mm = base_y_mm;
        header.base_z_mm = base_z_mm;
        header.occupancy_prob = occupancy_prob;
        header.quality_score = quality_score;
        header.point_count = static_cast<uint16_t>(points.size());

        size_t total_size = sizeof(VoxelHeader) + points.size() * sizeof(CompressedPoint3D);
        std::string buffer;
        buffer.resize(total_size);

        // 1. 拷贝 Header
        std::memcpy(&buffer[0], &header, sizeof(VoxelHeader));
        // 2. 拷贝压缩点数组 Payload
        if (!points.empty()) {
            std::memcpy(&buffer[sizeof(VoxelHeader)], points.data(), points.size() * sizeof(CompressedPoint3D));
        }

        return buffer;
    }

    // 从读取到的 Value (Slice / std::string) 中解析 Header 和点云数据
    static bool Parse(const Slice& value_slice, VoxelHeader* out_header, std::vector<CompressedPoint3D>* out_points) {
        if (value_slice.size() < sizeof(VoxelHeader)) {
            return false; // 二进制流损坏或残缺
        }

        // 1. 读取 Header
        std::memcpy(out_header, value_slice.data(), sizeof(VoxelHeader));

        // 2. 读取 Payload
        size_t expected_payload_size = out_header->point_count * sizeof(CompressedPoint3D);
        if (value_slice.size() < sizeof(VoxelHeader) + expected_payload_size) {
            return false;
        }

        out_points->resize(out_header->point_count);
        if (out_header->point_count > 0) {
            std::memcpy(out_points->data(), value_slice.data() + sizeof(VoxelHeader), expected_payload_size);
        }

        return true;
    }
};

#endif // VOXEL_VALUE_H