#include <iostream>
#include <string>
#include <cstdlib> // 用于 abort()
#include "kv_engine.h"

int main() {
    // 1. 初始化 KV 引擎，指定日志文件路径
    KVEngine<std::string, std::string> engine("./wal.log");

    std::cout << "==============================" << std::endl;
    std::cout << "  KV Engine Started!          " << std::endl;
    std::cout << "==============================" << std::endl;

    // 2. 尝试读取之前的数据（如果是崩溃后重启，这里应该能读到）
    std::string value;
    if (engine.get("name", value)) {
        std::cout << "[Recovery Success] Found 'name': " << value << std::endl;
    } else {
        std::cout << "[Info] No previous data found, this is a fresh start." << std::endl;
    }

    // 3. 写入新数据
    std::cout << "\n[Action] Writing new data to KV Engine..." << std::endl;
    engine.put("name", "requiem");
    engine.put("project", "kv-store");
    engine.put("status", "WAL-Tested");

    std::cout << "[Action] Data written to memory and WAL log!" << std::endl;

    // 4. 模拟系统崩溃（断电 / OOM / 被 kill -9）
    // 注意：这会导致程序瞬间死亡，跳过所有清理工作！
    std::cout << "\n[Simulating Crash] Calling abort() to simulate power loss..." << std::endl;

    // 如果注释掉上面的 std::abort()，程序就会正常退出。
    // 正常退出时，数据依然在 wal.log 中，下次启动同样能恢复。
    return 0;
}