# 1. 指定 CMake 最低版本要求
cmake_minimum_required(VERSION 3.10)

# 2. 定义项目名称和使用的语言 (CXX 代表 C++)
project(KVStore LANGUAGES CXX)

# 3. 设置 C++ 标准为 C++17，并禁止编译器使用扩展特性（保证代码通用性）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. 添加可执行文件目标
# 语法：add_executable(目标名称 源文件列表)
# 注意：模板类的实现虽然在 .h 中，但 main.cpp 和 kv_engine.cpp 仍需参与编译
add_executable(kv_server
    src/main.cpp
    src/kv_engine.cpp
)

# 5. 包含头文件目录
# 这样在代码里就可以直接写 #include "skip_list.h" 而不用写完整路径
target_include_directories(kv_server PRIVATE include)

# 6. (可选) 开启编译警告，帮助你在开发阶段发现潜在问题
if(MSVC)
    target_compile_options(kv_server PRIVATE /W4)
else()
    target_compile_options(kv_server PRIVATE -Wall -Wextra -pedantic)
endif()