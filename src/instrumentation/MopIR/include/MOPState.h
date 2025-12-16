#ifndef XSAN_MOP_IR_MOPSTATE_H
#define XSAN_MOP_IR_MOPSTATE_H

#include "MopContext.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Analysis/MemoryLocation.h"

namespace __xsan {
namespace MopIR {

// 与 Analysis/MopRecurrenceReducer.cpp 中的 MOPState 一致的分析状态，
// 但封装在 MopIR 内部，并额外接入 SCEV 用于循环不变性强化。
class MOPState {
public:
  MOPState(llvm::Function &F, llvm::AAResults &AA, llvm::DominatorTree &DT,
           llvm::PostDominatorTree &PDT, const llvm::TargetLibraryInfo &TLI,
           llvm::LoopInfo &LI, llvm::ScalarEvolution &SE);

  // 便捷构造：从 MopContext 抽取依赖
  explicit MOPState(MopContext &Ctx)
      : MOPState(Ctx.getFunction(), Ctx.getAAResults(), Ctx.getDominatorTree(),
                 Ctx.getPostDominatorTree(), Ctx.getTargetLibraryInfo(),
                 Ctx.getLoopInfo(), Ctx.getScalarEvolution()) {}

  // 判断 Current/Def 的依赖是否与循环无关（保证 AA 结果可用）
  bool isGuaranteedLoopIndependent(const llvm::Instruction *Current,
                                   const llvm::Instruction *KillingDef,
                                   const llvm::MemoryLocation &CurrentLoc);

  // 判断 Ptr 是否对任意循环都不变（含 SCEV 加强）
  bool isGuaranteedLoopInvariant(const llvm::Value *Ptr);

  // *_chk 精确长度强化
  llvm::LocationSize strengthenLocationSize(const llvm::Instruction *I,
                                            llvm::LocationSize Size) const;

  // 访问区间包含关系（主体逻辑移植自 Reducer::MOPState）
  bool isAccessRangeContains(const llvm::Instruction *KillingI,
                             const llvm::Instruction *DeadI,
                             const llvm::MemoryLocation &KillingLoc,
                             const llvm::MemoryLocation &DeadLoc,
                             int64_t &KillingOff, int64_t &DeadOff);

private:
  // 掩码存储是否完全覆盖
  bool isMaskedStoreOverwrite(const llvm::Instruction *KillingI,
                              const llvm::Instruction *DeadI);

private:
  llvm::Function &F;
  llvm::AAResults &AA;
  // 批量 AA（带 EarliestEscapeInfo 缓存）
  llvm::EarliestEscapeInfo EI;
  llvm::BatchAAResults BatchAA;

  llvm::DominatorTree &DT;
  llvm::PostDominatorTree &PDT;
  const llvm::TargetLibraryInfo &TLI;
  const llvm::DataLayout &DL;
  llvm::LoopInfo &LI;
  llvm::ScalarEvolution &SE;
  bool ContainsIrreducibleLoops;
  llvm::SmallPtrSet<const llvm::Value *, 32> EphValues;
};

} // namespace MopIR
} // namespace __xsan

#endif // XSAN_MOP_IR_MOPSTATE_H
