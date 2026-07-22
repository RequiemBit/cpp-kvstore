#include <iostream>
#include <string>
#include "kv_engine.h"

int main() {
    std::cout << "Starting KVStore Engine Test..." << std::endl;

    // 创建一个基于 string-key, string-value 的引擎
    KVEngine<std::string, std::string> engine;

    // 1. 测试插入 (Put)
    std::cout << "\n--- Inserting Data ---" << std::endl;
    engine.put("apple", "fruit");
    engine.put("banana", "yellow fruit");
    engine.put("carrot", "vegetable");
    engine.put("dog", "animal");
    
    // 打印内部结构看看层级分布
    engine.debug_print();

    // 2. 测试查询 (Get)
    std::cout << "\n--- Querying Data ---" << std::endl;
    std::string result;
    if (engine.get("banana", result)) {
        std::cout << "Found banana: " << result << std::endl;
    } else {
        std::cout << "Banana not found!" << std::endl;
    }

    if (engine.get("cat", result)) {
        std::cout << "Found cat: " << result << std::endl;
    } else {
        std::cout << "Cat not found (Expected)." << std::endl;
    }

    // 3. 测试更新 (Update / Overwrite)
    std::cout << "\n--- Updating Data ---" << std::endl;
    engine.put("apple", "red fruit"); // 更新 apple 的值
    engine.get("apple", result);
    std::cout << "Updated apple: " << result << std::endl;

    // 4. 测试删除 (Erase)
    std::cout << "\n--- Deleting Data ---" << std::endl;
    engine.erase("carrot");
    if (!engine.get("carrot", result)) {
        std::cout << "Carrot successfully deleted." << std::endl;
    }

    // 再次打印查看删除后的结构
    engine.debug_print();

    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}