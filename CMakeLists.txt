# 1. 指定 CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 2. 定义项目名称和使用的语言
project(KVStore LANGUAGES CXX)

# 3. 设置 C++ 标准为 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. 添加可执行文件目标
# 注意：这里列出了所有参与编译的 .cpp 文件
add_executable(kv_server
    src/main.cpp
    src/kv_engine.cpp
    src/wal_logger.cpp
    src/sstable_builder.cpp
    src/sstable_reader.cpp
    # 如果未来有 slice.cpp 实现，请在这里添加 src/slice.cpp
    # 目前 Slice 是纯头文件实现，不需要编译单元
)

# 5. 包含头文件目录
# 这样在代码里就可以直接写 #include "slice.h" 或 #include "wal_logger.h"
target_include_directories(kv_server PRIVATE include)

# 6. 针对 WSL/Linux 环境的编译优化与警告
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(kv_server PRIVATE 
        -Wall       # 开启常用警告
        -Wextra     # 开启额外警告
        -pedantic   # 严格遵循标准
        -O2         # 【新增】开启 O2 优化，这对存储引擎性能至关重要
    )
endif()
