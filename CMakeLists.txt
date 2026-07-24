# 1. 指定 CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 2. 定义项目名称和使用的语言
project(KVStore LANGUAGES CXX)

# 3. 设置 C++ 标准为 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. 添加可执行文件目标
add_executable(kv_server
    src/main.cpp
    src/kv_engine.cpp
    src/wal_logger.cpp
    src/sstable_builder.cpp
    src/sstable_reader.cpp
    
    # 将测试实现文件加入主程序，因为它要被 main.cpp 调用
    tests/test_runner.cpp 
)

# 5. 设置包含目录
# 注意：这里需要同时包含 include (业务头文件) 和 tests (测试头文件)
# 方法 A：显式添加 tests 目录
target_include_directories(kv_server PRIVATE include tests)

# 或者 方法 B（更通用）：添加项目根目录作为包含路径
# target_include_directories(kv_server PRIVATE ${CMAKE_SOURCE_DIR})


# 6. 针对 WSL/Linux 环境的编译优化与警告
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(kv_server PRIVATE 
        -Wall       # 开启常用警告
        -Wextra     # 开启额外警告
        -pedantic   # 严格遵循标准
        -O2         # 【新增】开启 O2 优化，这对存储引擎性能至关重要
    )
endif()
