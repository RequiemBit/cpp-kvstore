#include "kv_engine.h"


// 数据流向

// [用户代码]  engine.put("name", "requiem")
//       │
//       ▼
// [KVEngine]  获取互斥锁 (std::lock_guard)
//       │
//       ├──▶ 1. 泛型翻译官：to_string_internal("name")
//       │      将泛型 K/V 转换为 std::string ("name", "requiem")
//       │
//       ├──▶ 2. 零拷贝视图：std::string 隐式转换为 Slice
//       │      仅仅提取了 data_ 指针和 size_ 长度，不发生内存拷贝
//       │
//       ├──▶ 3. 落盘 (WAL)：wal_.Append(PUT, key_slice, val_slice)
//       │      将 Header + Key + Value 的二进制字节流追加写入磁盘
//       │
//       └──▶ 4. 更新内存：skiplist.put("name", "requiem")
//              将原始泛型数据深拷贝并插入到 SkipList 的内存树中


// [系统启动]  KVEngine 构造函数触发
//       │
//       ▼
// [WAL 恢复]  wal_.Recover()
//       │
//       ├──▶ 1. 读取二进制流：ifs.read()
//       │      从磁盘读出固定大小的 Header，得知 Key/Value 的长度
//       │
//       ├──▶ 2. 分配内存载体：std::string(len, '\0')
//       │      在堆上开辟对应大小的“空房间”
//       │
//       ├──▶ 3. 填充房间：ifs.read(key.data(), len)
//       │      将磁盘上的原始字节直接“砸”进 std::string 的内存中
//       │
//       └──▶ 4. 组装记录：返回 vector<ParsedLogRecord>
//              里面装满了 std::string 类型的 Key 和 Value
//       │
//       ▼
// [KVEngine]  遍历恢复出的记录
//       │
//       ├──▶ 5. 逆向翻译官：from_string_internal<K>(record.key)
//       │      利用 std::istringstream，将 std::string 重新解析回泛型 K/V
//       │
//       └──▶ 6. 重建内存结构：skiplist.put(key, val)
//              将恢复出的数据重新插入 SkipList，引擎恢复至崩溃前状态