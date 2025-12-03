#ifndef XSAN_MOP_IR_BUILDER_H
#define XSAN_MOP_IR_BUILDER_H

#include "Mop.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

namespace __xsan {
namespace MopIR {

// LLVM IR到MOP的转换器
class MopBuilder {
private:
  llvm::Function& F;

  
  // 地址分析，提取基地址和偏移量
  std::pair<llvm::Value*, llvm::APInt> analyzeAddress(llvm::Value* Ptr) const;
  
  // 创建MOP对象
  std::unique_ptr<Mop> createMop(llvm::Instruction* Inst) const;

public:
  MopBuilder(llvm::Function& Func)
    : F(Func) {}
  
  // 将整个函数转换为MOP列表
  MopList buildMopList();
  
  // 将单个指令转换为MOP
  std::unique_ptr<Mop> buildMop(llvm::Instruction* Inst);
};

} // namespace MopIR
} // namespace __xsan

#endif // XSAN_MOP_IR_BUILDER_H