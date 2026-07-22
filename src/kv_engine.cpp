#include "kv_engine.h"

// 目前所有逻辑都在头文件中实现（因为它们是模板）。
// 这个文件保留作为编译单元，未来可以在此处添加非模板的全局函数或辅助逻辑。

// 如果你不想把模板实现放在头文件，你需要在这里为特定类型进行“显式实例化”：
// template class KVEngine<std::string, std::string>;
// template class KVEngine<int, int>;