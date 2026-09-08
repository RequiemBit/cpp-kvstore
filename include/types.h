#pragma once

#include "slice.h"

struct TableValue {
    ValueType type{ValueType::kTypeValue}; // 1. 补上 type 成员
    std::string value;                      // 2. 补上 value 成员

    // 默认构造函数
    TableValue() = default;

    // 常用构造函数：接受 type 和 value（解决 TableValue{ValueType::kTypeValue, value} 报错）
    TableValue(ValueType t, std::string val) 
        : type(t), value(std::move(val)) {}

    // 单参数构造函数：默认类型为 kTypeValue（解决 WAL 恢复时的单参数调用）
    explicit TableValue(std::string val) 
        : type(ValueType::kTypeValue), value(std::move(val)) {}

    // 友元输出重载
    friend std::ostream& operator<<(std::ostream& os, const TableValue& tv) {
        os << (tv.type == ValueType::kTypeDeletion ? "[Tombstone]" : tv.value);
        return os;
    }
};