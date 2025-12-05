#ifndef XSAN_MOP_IR_MOP_H
#define XSAN_MOP_IR_MOP_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

namespace __xsan {
namespace MopIR {

// 内存操作类型枚举
enum class MopType {
  Load,      // 读操作
  Store,     // 写操作
  Atomic,    // 原子操作
  Memcpy,    // 内存拷贝
  Memset     // 内存设置
};

// MOP基类
class Mop {
private:
  MopType Type;                           // 操作类型
  llvm::MemoryLocation Location;          // 内存位置（使用LLVM官方实现）
  llvm::Instruction* OriginalInst;        // 原始LLVM指令
  llvm::SmallVector<Mop*, 4> Dependencies; // 依赖的其他MOP

public:
  Mop(MopType Ty, const llvm::MemoryLocation& Loc, llvm::Instruction* Inst)
    : Type(Ty), Location(Loc), OriginalInst(Inst) {}
  
  // 访问器方法
  MopType getType() const { return Type; }
  const llvm::MemoryLocation& getLocation() const { return Location; }
  llvm::Instruction* getOriginalInst() const { return OriginalInst; }
  
  // 判断是否可以与其他MOP合并
  bool canMergeWith(const Mop* Other) const;
  
  // 合并两个MOP
  std::unique_ptr<Mop> mergeWith(const Mop* Other) const;
  
  // 添加依赖关系
  void addDependency(Mop* Dep) { Dependencies.push_back(Dep); }
  
  // 获取依赖列表
  const llvm::SmallVectorImpl<Mop*>& getDependencies() const { return Dependencies; }
  
  // 打印MOP信息
  void print(llvm::raw_ostream& OS) const;
};

// MOP集合
using MopList = llvm::SmallVector<std::unique_ptr<Mop>, 16>;

} // namespace MopIR
} // namespace __xsan

#endif // XSAN_MOP_IR_MOP_H