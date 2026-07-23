#include <iostream>
#include <vector>
#include <cassert>
#include "slice.h"
#include "wal_logger.h"

int main() {
    std::cout << "=== 开始测试 Slice 与 WalLogger 集成 ===" << std::endl;

    // 1. 初始化 WAL 日志器
    WalLogger logger("./test_wal.log");

    // 2. 测试用例数据（包含正常字符串、空字符串、包含'\0'的二进制数据）
    struct TestCase {
        OperationType op;
        std::string key;
        std::string value;
    };

    std::vector<TestCase> test_cases = {
        {OperationType::PUT, "normal_key", "normal_value"},
        {OperationType::PUT, "empty_value_key", ""},      // 测试空 Value
        {OperationType::PUT, "", "empty_key_value"},      // 测试空 Key
        {OperationType::ERASE, "to_be_deleted", ""},      // 测试删除操作
        {OperationType::PUT, "binary_key", std::string("val\0ue", 6)} // 测试包含 '\0' 的二进制数据
    };

    // 3. 使用 Slice 进行写入测试
    std::cout << "[Test] 写入数据中..." << std::endl;
    for (const auto& tc : test_cases) {
        // 将 std::string 隐式转换为 Slice（零拷贝）
        Slice key_slice(tc.key);
        Slice val_slice(tc.value);
        
        bool success = logger.Append(tc.op, key_slice, val_slice);
        assert(success && "写入 WAL 失败！");
    }
    std::cout << "[Test] 写入成功，共 " << test_cases.size() << " 条记录。" << std::endl;

    // 4. 读取并验证恢复的数据
    std::cout << "[Test] 开始恢复数据..." << std::endl;
    auto records = logger.Recover();
    
    assert(records.size() == test_cases.size() && "恢复的记录数量不匹配！");

    for (size_t i = 0; i < records.size(); ++i) {
        const auto& rec = records[i];
        const auto& expected = test_cases[i];
        
        // 验证操作类型
        assert(rec.op == expected.op && "操作类型不匹配！");
        // 验证 Key（将恢复出的 Slice 转回 string 进行比较）
        assert(rec.key == expected.key && "Key 不匹配！");
        // 验证 Value
        assert(rec.value == expected.value && "Value 不匹配！");
        
        std::cout << "  -> 验证通过: Op=" << (int)rec.op 
                  << ", Key=\"" << rec.key << "\"" 
                  << ", Value=\"" << rec.value << "\"" << std::endl;
    }

    std::cout << "\n🎉 恭喜！所有 Slice 与 WalLogger 集成测试全部通过！" << std::endl;
    return 0;
}