# 1. 指定 CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 2. 定义项目名称和使用的语言
project(KVStore LANGUAGES CXX)

# 1：全局默认开启 Release 优化模式
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

# 3. 设置 C++ 标准为 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# 2：设置全局编译选项（加入 -g 开启调试符号，-fno-omit-frame-pointer 保留栈帧指针）
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # -O3: 最强性能优化
    # -g: 生成调试符号（让 perf 能看懂函数名，告别 [unknown]）
    # -fno-omit-frame-pointer: 保留栈帧指针，让火焰图的调用栈层级100%精准
    # -DNDEBUG: 禁用断言
    add_compile_options(-O3 -g -fno-omit-frame-pointer -DNDEBUG -Wall -Wextra)
endif()

# 4. 定义存储引擎核心源文件（不包含 main.cpp 和测试代码）
set(ENGINE_SOURCES
    src/kv_engine.cpp
    src/wal_logger.cpp
    src/sstable_builder.cpp
    src/sstable_reader.cpp
    src/iterator.cpp
)

# 5. 创建静态库 kv_store_lib (核心组件 - 现在它也能吃满 -O3 优化了！)
add_library(kv_store_lib STATIC ${ENGINE_SOURCES})
target_include_directories(kv_store_lib PUBLIC include)

# 6. 构建主程序与单元测试目标: kv_server
add_executable(kv_server
    src/main.cpp
    tests/test_runner.cpp 
)
target_include_directories(kv_server PRIVATE include tests)
target_link_libraries(kv_server PRIVATE kv_store_lib pthread)

# 7. 构建 Google Benchmark 压测目标: kv_benchmark
find_package(benchmark QUIET)

if(benchmark_FOUND)
    message(STATUS "Google Benchmark found. Building kv_benchmark target...")
    
    add_executable(kv_benchmark
        tests/bench_kv.cpp
    )

    target_include_directories(kv_benchmark PRIVATE include tests)
    target_link_libraries(kv_benchmark PRIVATE kv_store_lib benchmark::benchmark pthread)
else()
    message(WARNING "Google Benchmark NOT found! 'kv_benchmark' target will be skipped.\n"
                    "Please install it via: 'sudo apt-get install libbenchmark-dev'")
endif()

# 8. 构建 500 万级大数据量测试目标: kv_large_scale
add_executable(kv_large_scale
    tests/test_large_scale.cpp
)
target_include_directories(kv_large_scale PRIVATE include tests)
target_link_libraries(kv_large_scale PRIVATE kv_store_lib pthread)

# 9. 构建长时间稳定性测试目标: kv_soak
add_executable(kv_soak
    tests/test_soak.cpp
)
target_include_directories(kv_soak PRIVATE include tests)
target_link_libraries(kv_soak PRIVATE kv_store_lib pthread)

# 10. 3D 空间 KV 功能测试
add_executable(test_spatial_3d 
    tests/test_spatial_3d.cpp
)
target_include_directories(test_spatial_3d PRIVATE include tests)
target_link_libraries(test_spatial_3d PRIVATE kv_store_lib pthread)

# 11. 范围查询功能测试
add_executable(test_range_search 
    tests/test_range_search.cpp
)
target_include_directories(test_range_search PRIVATE include tests)
target_link_libraries(test_range_search PRIVATE kv_store_lib pthread)