#ifndef MOP_IR_IMPLEMENTATION_MOP_DATA_H
#define MOP_IR_IMPLEMENTATION_MOP_DATA_H

/**
 * MOP (Memory Operation) 数据结构定义
 * 
 * 这里定义 MOP 的核心数据结构，可以基于原有 MopIR 的实现
 * 但需要适配到新的框架中
 */

#include <vector>
#include <memory>
#include <string>

namespace MopIRImpl {
namespace MOP {

// 内存操作类型
enum class MopType {
  Load,      // 读操作
  Store,     // 写操作
  Atomic,    // 原子操作
  Memcpy,    // 内存拷贝
  Memset     // 内存设置
};

// MOP 数据结构
class Mop {
private:
  MopType Type;
  // 其他字段：内存位置、原始指令等
  // 可以根据原有 MopIR 的实现添加更多字段
  // ...
  
public:
  Mop(MopType Ty) : Type(Ty), IsRedundant(false) {}
  
  MopType getType() const { return Type; }
  bool isWrite() const {
    return Type == MopType::Store || 
           Type == MopType::Memset || 
           Type == MopType::Memcpy;
  }
  bool isRead() const {
    return Type == MopType::Load;
  }
  
  // 冗余标记（用于优化）
  bool IsRedundant;
  void setRedundant(bool r) { IsRedundant = r; }
  bool isRedundant() const { return IsRedundant; }
};

// MOP 列表
using MopList = std::vector<std::unique_ptr<Mop>>;

} // namespace MOP
} // namespace MopIRImpl

#endif // MOP_IR_IMPLEMENTATION_MOP_DATA_H

