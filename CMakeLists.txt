# 1. 指定 CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 2. 定义项目名称和使用的语言
project(KVStore LANGUAGES CXX)

# 3. 设置 C++ 标准为 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. 定义存储引擎核心源文件（不包含 main.cpp 和测试代码）
set(ENGINE_SOURCES
    src/kv_engine.cpp
    src/wal_logger.cpp
    src/sstable_builder.cpp
    src/sstable_reader.cpp
    src/iterator.cpp
)

# 5. 构建主程序与单元测试目标: kv_server
add_executable(kv_server
    src/main.cpp
    ${ENGINE_SOURCES}
    tests/test_runner.cpp 
)

# 设置包含目录 (业务头文件 + 测试头文件)
target_include_directories(kv_server PRIVATE include tests)

# 6. 设置编译选项（开启 -O2 性能优化及警告）
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(kv_server PRIVATE 
        -Wall 
        -Wextra 
        -pedantic 
        -O2
    )
endif()

# 7. 构建 Google Benchmark 压测目标: kv_benchmark
# 检查系统是否安装了 Google Benchmark
find_package(benchmark QUIET)

if(benchmark_FOUND)
    message(STATUS "Google Benchmark found. Building kv_benchmark target...")
    
    add_executable(kv_benchmark
        tests/bench_kv.cpp
        ${ENGINE_SOURCES}
    )

    target_include_directories(kv_benchmark PRIVATE include tests)
    target_link_libraries(kv_benchmark PRIVATE benchmark::benchmark pthread)

    if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(kv_benchmark PRIVATE -O2 -Wall -Wextra)
    endif()
else()
    message(WARNING "Google Benchmark NOT found! 'kv_benchmark' target will be skipped.\n"
                    "Please install it via: 'sudo apt-get install libbenchmark-dev'")
endif()


# 8. 构建 500 万级大数据量测试目标: kv_large_scale
add_executable(kv_large_scale
    tests/test_large_scale.cpp
    ${ENGINE_SOURCES}
)

target_include_directories(kv_large_scale PRIVATE include tests)
target_link_libraries(kv_large_scale PRIVATE pthread)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(kv_large_scale PRIVATE -O3 -Wall -Wextra)
endif()

# 9. 构建长时间稳定性测试目标: kv_soak
add_executable(kv_soak
    tests/test_soak.cpp
    ${ENGINE_SOURCES}
)

target_include_directories(kv_soak PRIVATE include tests)
target_link_libraries(kv_soak PRIVATE pthread)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(kv_soak PRIVATE -O3 -Wall -Wextra)
endif()