#include "test_runner.h"

int main() {
    // 运行全套单元与集成测试
    kvstore::test::RunAllTests();
    return 0;
}