#ifndef SLICE_H
#define SLICE_H

#include <cassert>
#include <cstddef>
#include <cstring>
#include <string>
#include <iostream>

/**
 * @brief 工业级 Slice 类（参考 LevelDB 设计）
 * 零拷贝的数据视图类，仅持有指向外部内存的指针和长度。
 * 注意：Slice 本身不持有内存生命周期，使用者需保证底层数据的存活时间长于 Slice。
 */
class Slice {
private:
    const char* data_; // 数据起始地址
    size_t size_;      // 数据字节长度

public:
    // ------------------- 1. 构造与析构 -------------------
    
    // 默认构造：空 Slice
    Slice() : data_(""), size_(0) {}

    // 底层构造：指针 + 长度
    Slice(const char* d, size_t s) : data_(d), size_(s) {}

    // 隐式构造：支持 C 风格字符串常量，如 "hello"
    Slice(const char* s) : data_(s), size_(std::strlen(s)) {}

    // 隐式构造：支持 std::string，零拷贝！
    Slice(const std::string& s) : data_(s.data()), size_(s.size()) {}

    ~Slice() = default;

    // ------------------- 2. 基础属性访问 -------------------
    
    const char* data() const { return data_; }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    // 下标访问（带有断言保护）
    char operator[](size_t n) const {
        assert(n < size());
        return data_[n];
    }

    // ------------------- 3. 常用操作函数 -------------------
    
    // 清空视图
    void clear() {
        data_ = "";
        size_ = 0;
    }

    // 移除前 n 个字节（常用于协议解析/前缀剥离）
    void remove_prefix(size_t n) {
        assert(n <= size());
        data_ += n;
        size_ -= n;
    }

    // 是否以指定前缀开头
    bool starts_with(const Slice& x) const {
        return (size_ >= x.size_) && (std::memcmp(data_, x.data_, x.size_) == 0);
    }

    // 转回 std::string（深拷贝，用于长期持有数据）
    std::string to_string() const {
        return std::string(data_, size_);
    }

    // ------------------- 4. 字典序比较（核心性能点） -------------------
    
    /**
     * @brief 比较两个 Slice 的字典序（使用 memcmp 极其快速）
     * @return < 0  : *this <  b
     *         == 0 : *this == b
     *         > 0  : *this >  b
     */
    int compare(const Slice& b) const {
        const size_t min_len = (size_ < b.size_) ? size_ : b.size_;
        int r = std::memcmp(data_, b.data_, min_len);
        if (r == 0) {
            if (size_ < b.size_) return -1;
            if (size_ > b.size_) return 1;
            return 0;
        }
        return r;
    }

    // 运算符重载，方便各种条件判断与排序
    bool operator==(const Slice& rhs) const { return compare(rhs) == 0; }
    bool operator!=(const Slice& rhs) const { return compare(rhs) != 0; }
    bool operator<(const Slice& rhs) const  { return compare(rhs) < 0; }
    bool operator<=(const Slice& rhs) const { return compare(rhs) <= 0; }
    bool operator>(const Slice& rhs) const  { return compare(rhs) > 0; }
    bool operator>=(const Slice& rhs) const { return compare(rhs) >= 0; }
};

// 友元/全局流输出重载，方便 std::cout 调试打印
inline std::ostream& operator<<(std::ostream& os, const Slice& s) {
    return os << s.to_string();
}

#endif // SLICE_H








// slice的作用
// 1. wal_.Append(k_str, v_str); 隐式将string构造为slice，零拷贝

// 后续
// 2. 读取与解析链路（Read & Parse Path）：极其高效的协议解析
// 这是 Slice 最强大的应用场景之一。当你的 WAL 从磁盘读出一大块二进制数据（比如 4KB），你需要解析出里面的 Header、Key、Value。
// 没有 Slice：你可能需要不断地调用 std::string::substr()，每次截取都会触发内存分配和深拷贝，产生大量临时对象。
// 有 Slice：你只需要调用 slice.remove_prefix(header_size)，仅仅把内部指针向后移动几个字节，耗时为 O(1)！这在解析复杂的二进制协议时，性能提升是指数级的。
// 3. 比较与排序链路（Compare Path）：SkipList 的极速查找
// 你的 SkipList 在插入和查找时，需要频繁比较 Key 的大小。
// 没有 Slice：如果是 std::string 比较，C++ 标准库内部不仅要逐字节比较，还要在每次比较前检查长度。
// 有 Slice：我们在 Slice 中直接用 C 语言的 memcmp 进行内存块比较。这是 CPU 级别的高度优化指令，在海量数据排序和范围查询（Range Scan）时，速度极快。
// 4. 未来的 SSTable 链路（Block 解析）：零拷贝读取磁盘
// 当你未来实现 LSM-Tree 的 SSTable 时，你会把数据切分成一个个 Block 放在磁盘上。
// 当用户 Get("key") 时，你把包含该 Key 的 Block 读入内存。此时，Block 里的 Key 和 Value 就静静地躺在这块内存中。
// 有 Slice：你直接构造一个 Slice 指向 Block 内存中的某个位置，返回给用户。全程没有发生任何内存分配！
// 没有 Slice：你必须把 Block 里的字节抠出来，new 一个 std::string，再拷贝进去返回。