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
#include <cstdint>

#ifdef MOP_IR_USE_LLVM
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/Instruction.h"
#endif

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
  
#ifdef MOP_IR_USE_LLVM
  // LLVM 相关字段
  const llvm::Instruction* Inst;  // 原始指令指针
  llvm::MemoryLocation MemLoc;    // 内存位置信息
#endif
  
  // 通用字段
  void* Ptr;              // 内存地址指针（通用版本）
  uint64_t Size;          // 访问大小（字节）
  int64_t Offset;         // 偏移量
  
  // 冗余检测相关
  bool IsRedundant;
  Mop* CoveringMop;       // 覆盖此 MOP 的另一个 MOP（如果冗余）
  
public:
#ifdef MOP_IR_USE_LLVM
  // LLVM 版本构造函数
  Mop(MopType Ty, const llvm::Instruction* I, const llvm::MemoryLocation& Loc)
    : Type(Ty), Inst(I), MemLoc(Loc), Ptr(nullptr), Size(0), Offset(0),
      IsRedundant(false), CoveringMop(nullptr) {
    if (Loc.Size.hasValue()) {
      Size = Loc.Size.getValue();
    }
  }
#endif
  
  // 通用版本构造函数
  Mop(MopType Ty, void* ptr = nullptr, uint64_t size = 0, int64_t offset = 0)
    : Type(Ty), Ptr(ptr), Size(size), Offset(offset),
      IsRedundant(false), CoveringMop(nullptr)
#ifdef MOP_IR_USE_LLVM
    , Inst(nullptr)
#endif
  {}
  
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
  void setRedundant(bool r) { IsRedundant = r; }
  bool isRedundant() const { return IsRedundant; }
  
  // 覆盖关系
  void setCoveringMop(Mop* mop) { CoveringMop = mop; }
  Mop* getCoveringMop() const { return CoveringMop; }
  
#ifdef MOP_IR_USE_LLVM
  // LLVM 特定访问器
  const llvm::Instruction* getInstruction() const { return Inst; }
  const llvm::MemoryLocation& getMemoryLocation() const { return MemLoc; }
  void setMemoryLocation(const llvm::MemoryLocation& Loc) { MemLoc = Loc; }
#endif
  
  // 通用访问器
  void* getPointer() const { return Ptr; }
  uint64_t getSize() const { return Size; }
  int64_t getOffset() const { return Offset; }
  void setSize(uint64_t s) { Size = s; }
  void setOffset(int64_t o) { Offset = o; }
};

// MOP 列表
using MopList = std::vector<std::unique_ptr<Mop>>;

} // namespace MOP
} // namespace MopIRImpl

#endif // MOP_IR_IMPLEMENTATION_MOP_DATA_H

