#include <iterator>
#include <optional>
#include <optional>

#include "../include/MopAnalysis.h"
#include "../include/MopContext.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Operator.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Support/TypeSize.h"
#include "llvm/Support/raw_ostream.h"

using namespace __xsan::MopIR;
using namespace llvm;

namespace {
// Mirror helper from MopRecurrenceReducer: derive precise object size if known.
std::optional<TypeSize> getPointerSize(const Value *V, const DataLayout &DL,
                                       const TargetLibraryInfo &TLI,
                                       const Function *F) {
  uint64_t Size;
  ObjectSizeOpts Opts;
  Opts.NullIsUnknownSize = NullPointerIsDefined(F);

  if (getObjectSize(V, Size, DL, &TLI, Opts)) {
    return TypeSize::getFixed(Size);
  }
  return std::nullopt;
}
} // namespace

// ============================================================================
// MopAnalysisPipeline 实现
// ============================================================================

void MopAnalysisPipeline::run(const MopList& Mops) {
  if (!Context) {
    return;  // 没有上下文，无法运行
  }
  
  // 依次运行每个分析器
  for (auto& Analysis : Analyses) {
    if (Analysis) {
      Analysis->analyze(Mops);
    }
  }
}

// ============================================================================
// MopAliasAnalysis 实现（占位实现）
// ============================================================================

void MopAliasAnalysis::analyze(const MopList& Mops) {
  if (!Context) {
    return;
  }
  
  // TODO: 实现别名分析逻辑
  // 这里先清空之前的结果
  AliasResults.clear();
  
  // 遍历所有 MOP 对，进行别名分析
  for (size_t i = 0; i < Mops.size(); ++i) {
    for (size_t j = i + 1; j < Mops.size(); ++j) {
      Mop* M1 = Mops[i].get();
      Mop* M2 = Mops[j].get();
      
      if (!M1 || !M2) continue;
      
      // 使用 AliasAnalysis 进行查询
      auto& AA = Context->getAAResults();
      AliasResult AR = AA.alias(M1->getLocation(), M2->getLocation());
      
      // 存储结果（NoAlias = false, 其他 = true）
      bool IsAliased = (AR != AliasResult::NoAlias);
      AliasResults[{M1, M2}] = IsAliased;
      AliasResults[{M2, M1}] = IsAliased;  // 对称关系
    }
  }
}

bool MopAliasAnalysis::isAliased(const Mop* M1, const Mop* M2) const {
  auto It = AliasResults.find({M1, M2});
  if (It != AliasResults.end()) {
    return It->second;
  }
  return false;  // 默认假设不别名
}

// ============================================================================
// MopDataflowAnalysis 实现（占位实现）
// ============================================================================

void MopDataflowAnalysis::analyze(const MopList& Mops) {
  if (!Context) {
    return;
  }
  
  // TODO: 实现数据流分析逻辑
  // 这里先留空，后续可以添加数据流分析
}

// ============================================================================
// MopDominanceAnalysis 实现
// ============================================================================

void MopDominanceAnalysis::analyze(const MopList& Mops) {
  if (!Context) {
    return;
  }
  
  // 清空之前的结果
  DominanceCache.clear();
  PostDominanceCache.clear();
  
  auto& DT = Context->getDominatorTree();
  auto& PDT = Context->getPostDominatorTree();
  
  // 遍历所有 MOP 对，进行支配关系分析
  for (size_t i = 0; i < Mops.size(); ++i) {
    for (size_t j = 0; j < Mops.size(); ++j) {
      if (i == j) continue;
      
      Mop* M1 = Mops[i].get();
      Mop* M2 = Mops[j].get();
      
      if (!M1 || !M2 || !M1->getOriginalInst() || !M2->getOriginalInst()) {
        continue;
      }
      
      Instruction* I1 = M1->getOriginalInst();
      Instruction* I2 = M2->getOriginalInst();
      
      // 检查支配关系
      bool Dom = DT.dominates(I1, I2);
      DominanceCache[{M1, M2}] = Dom;
      
      // 检查后支配关系
      bool PostDom = PDT.dominates(I1, I2);
      PostDominanceCache[{M1, M2}] = PostDom;
    }
  }
}

bool MopDominanceAnalysis::dominates(const Mop* Mop1, const Mop* Mop2) const {
  auto It = DominanceCache.find({Mop1, Mop2});
  if (It != DominanceCache.end()) {
    return It->second;
  }
  return false;
}

bool MopDominanceAnalysis::postDominates(const Mop* Mop1, const Mop* Mop2) const {
  auto It = PostDominanceCache.find({Mop1, Mop2});
  if (It != PostDominanceCache.end()) {
    return It->second;
  }
  return false;
}

bool MopDominanceAnalysis::dominatesOrPostDominates(const Mop* Mop1, const Mop* Mop2) const {
  return dominates(Mop1, Mop2) || postDominates(Mop1, Mop2);
}

// ============================================================================
// MopRedundancyAnalysis 实现（占位实现）
// ============================================================================

//Traverse the Mop list and find redundant MOPs.
//ref: [distillRecurringChecks] in MopRecurrenceReducer.cpp
void MopRedundancyAnalysis::analyze(const MopList& Mops) {
  if (!Context) {
    return;
  }
  
  // 清空之前的结果
  RedundantMops.clear();
  CoveringMopMap.clear();

  // 两两检查可覆盖关系，满足覆盖则把 Later 记为冗余
  for (const auto &KMopUPtr : Mops) {
    Mop *Killing = KMopUPtr.get();
    if (!Killing || !Killing->getOriginalInst() || Killing->isRedundant()) {
      continue;
    }

    for (const auto &DMopUPtr : Mops) {
      Mop *Dead = DMopUPtr.get();
      if (!Dead || Killing == Dead || !Dead->getOriginalInst()) {
        continue;
      }

      if (!doesMopCover(Killing, Dead)) {
        continue;
      }

      RedundantMops.insert(Dead);
      CoveringMopMap[Dead] = Killing;
      Dead->setRedundant(true);
      Dead->setCoveringMop(Killing);
    }
  }
}

bool MopRedundancyAnalysis::isRedundant(const Mop* M) const {
  return RedundantMops.contains(M);
}

const Mop* MopRedundancyAnalysis::getCoveringMop(const Mop* M) const {
  auto It = CoveringMopMap.find(M);
  if (It != CoveringMopMap.end()) {
    return It->second;
  }
  return nullptr;
}

//return whether Mop1 can cover Mop2.
//ref: [isMopCheckRecurring] in MopRecurrenceReducer.cpp
bool MopRedundancyAnalysis::doesMopCover(const Mop* Mop1, const Mop* Mop2) const {
  if (!Context || !Mop1 || !Mop2) {
    return false;
  }

  auto *I1 = Mop1->getOriginalInst();
  auto *I2 = Mop2->getOriginalInst();
  if (!I1 || !I2) {
    return false;
  }

  // 写操作可以覆盖任意，读操作仅覆盖读操作
  if (!Mop1->isWrite() && Mop1->isWrite() != Mop2->isWrite()) {
    return false;
  }

  int64_t Off1 = 0;
  int64_t Off2 = 0;
  if (!isAccessRangeContains(Mop1, Mop2, Off1, Off2)) {
    return false;
  }

  if (!doesDominateOrPostDominate(Mop1, Mop2)) {
    return false;
  }

  if (hasInterferingCallBetween(Mop1, Mop2)) {
    return false;
  }

  return true;
}

bool MopRedundancyAnalysis::hasInterferingCallBetween(const Mop* Earlier, const Mop* Later) const {
  if (!Earlier || !Later) {
    return true;  // 保守：无法判断则认为存在干扰
  }

  Instruction *I1 = Earlier->getOriginalInst();
  Instruction *I2 = Later->getOriginalInst();
  if (!I1 || !I2) {
    return true;
  }

  // 仅在同一基本块内进行扫描，跨基本块保守处理
  if (I1->getParent() != I2->getParent()) {
    return true;
  }

  BasicBlock *BB = I1->getParent();

  // 确定遍历方向
  Instruction *Begin = I1;
  Instruction *End = I2;
  if (!I1->comesBefore(I2)) {
    std::swap(Begin, End);
  }

  // 在 Begin 之后、End 之前查找可能写内存或有副作用的调用
  for (auto It = std::next(Begin->getIterator()); It != End->getIterator(); ++It) {
    Instruction &Cur = *It;
    if (isa<DbgInfoIntrinsic>(&Cur)) {
      continue;
    }
    if (auto *CB = dyn_cast<CallBase>(&Cur)) {
      // 忽略显式标记 no-sanitize 的调用
      if (Cur.getMetadata("nosanitize")) {
        continue;
      }
      if (CB->mayHaveSideEffects()) {
        return true;
      }
    }
    if (Cur.mayHaveSideEffects()) {
      return true;
    }
  }

  return false;
}

bool MopRedundancyAnalysis::isGuaranteedLoopInvariant(const Value* Ptr) const {
  if (!Context) {
    return false;
  }
  auto &LI = Context->getLoopInfo();

  Ptr = Ptr->stripPointerCasts();
  if (const auto *GEP = dyn_cast<GEPOperator>(Ptr)) {
    if (GEP->hasAllConstantIndices()) {
      Ptr = GEP->getPointerOperand()->stripPointerCasts();
    }
  }

  if (const auto *I = dyn_cast<Instruction>(Ptr)) {
    // 位于入口块或者不在任何循环中则视为循环不变
    return I->getParent()->isEntryBlock() || !LI.getLoopFor(I->getParent());
  }
  return true;
}

bool MopRedundancyAnalysis::isGuaranteedLoopIndependent(
    const Instruction* Current, const Instruction* KillingDef,
    const MemoryLocation& CurrentLoc) const {
  if (!Context || !Current || !KillingDef) {
    return false;
  }
  auto &LI = Context->getLoopInfo();

  // 同一基本块内必然可安全比较
  if (Current->getParent() == KillingDef->getParent()) {
    return true;
  }

  const Loop *CurrentLoop = LI.getLoopFor(Current->getParent());
  if (CurrentLoop && CurrentLoop == LI.getLoopFor(KillingDef->getParent())) {
    return true;
  }

  // 其他情况需要保证指针在所有循环中不变
  return isGuaranteedLoopInvariant(CurrentLoc.Ptr);
}

LocationSize MopRedundancyAnalysis::strengthenLocationSize(
    const Instruction* I, LocationSize Size) const {
  if (!Context || !I) {
    return Size;
  }
  if (const auto *CB = dyn_cast<CallBase>(I)) {
    LibFunc F;
    const auto &TLI = Context->getTargetLibraryInfo();
    if (TLI.getLibFunc(*CB, F) && TLI.has(F) &&
        (F == LibFunc_memset_chk || F == LibFunc_memcpy_chk)) {
      if (const auto *Len = dyn_cast<ConstantInt>(CB->getArgOperand(2))) {
        return LocationSize::precise(Len->getZExtValue());
      }
    }
  }
  return Size;
}

//ref: [isAccessRangeContains] in MopRecurrenceReducer.cpp
bool MopRedundancyAnalysis::isAccessRangeContains(const Mop* Mop1, const Mop* Mop2,
                                                    int64_t& Off1, int64_t& Off2) const {
  if (!Context || !Mop1 || !Mop2) {
    return false;
  }

  const auto &Loc1 = Mop1->getLocation();
  const auto &Loc2 = Mop2->getLocation();

  auto &AA = Context->getAAResults();
  const DataLayout &DL = Context->getDataLayout();
  const auto &TLI = Context->getTargetLibraryInfo();

  const Instruction *KillingI = Mop1->getOriginalInst();
  const Instruction *DeadI = Mop2->getOriginalInst();

  if (!isGuaranteedLoopIndependent(DeadI, KillingI, Loc2)) {
    return false;
  }

  LocationSize KillingSize = strengthenLocationSize(KillingI, Loc1.Size);
  const Value *DeadPtr = Loc2.Ptr->stripPointerCasts();
  const Value *KillingPtr = Loc1.Ptr->stripPointerCasts();
  const Value *DeadUndObj = getUnderlyingObject(DeadPtr);
  const Value *KillingUndObj = getUnderlyingObject(KillingPtr);

  if (DeadUndObj == KillingUndObj && KillingSize.isPrecise() &&
      isIdentifiedObject(KillingUndObj)) {
    if (auto ObjSize = getPointerSize(KillingUndObj, DL, TLI,
                                      &Context->getFunction())) {
      if (*ObjSize == KillingSize.getValue()) {
        return true;
      }
    }
  }

  // 尝试处理不精确的大小（例如 MemIntrinsic）
  if (!KillingSize.isPrecise() || !Loc2.Size.isPrecise()) {
    const auto *KillingMemI = dyn_cast_or_null<MemIntrinsic>(KillingI);
    const auto *DeadMemI = dyn_cast_or_null<MemIntrinsic>(DeadI);
    if (KillingMemI && DeadMemI) {
      const Value *KillingLen = KillingMemI->getLength();
      const Value *DeadLen = DeadMemI->getLength();
      if (KillingLen == DeadLen && AA.isMustAlias(Loc2, Loc1)) {
        return true;
      }
    }
    return false;
  }

  AliasResult AR = AA.alias(Loc1, Loc2);
  if (AR == AliasResult::NoAlias) {
    return false;
  }

  uint64_t Size1 = KillingSize.getValue();
  uint64_t Size2 = Loc2.Size.getValue();

  if (DeadUndObj != KillingUndObj) {
    return false;
  }

  int64_t BaseOff1 = 0;
  int64_t BaseOff2 = 0;
  const Value *Base1 =
      GetPointerBaseWithConstantOffset(KillingPtr, BaseOff1, DL);
  const Value *Base2 = GetPointerBaseWithConstantOffset(DeadPtr, BaseOff2, DL);

  // MustAlias: 只需比较大小
  if (AR == AliasResult::MustAlias) {
    Off1 = BaseOff1;
    Off2 = BaseOff2;
    return Size1 >= Size2;
  }

  // 如果能分解出同一基指针，则用偏移与大小判断包含关系
  if (Base1 && Base2 && Base1 == Base2) {
    Off1 = BaseOff1;
    Off2 = BaseOff2;
    return (Off1 <= Off2) &&
           (static_cast<uint64_t>(Off2 - Off1) + Size2 <= Size1);
  }

  // PartialAlias 带偏移的情况（如果分析器提供偏移）
  if (AR == AliasResult::PartialAlias && AR.hasOffset()) {
    int32_t Off = AR.getOffset();
    if (Off >= 0 && static_cast<uint64_t>(Off) + Size2 <= Size1) {
      Off1 = 0;
      Off2 = Off;
      return true;
    }
  }

  // 无法精确判断时，返回false
  return false;
}

bool MopRedundancyAnalysis::doesDominateOrPostDominate(const Mop* Mop1, const Mop* Mop2) const {
  if (!Context) {
    return false;
  }
  
  if (!Mop1 || !Mop2 || !Mop1->getOriginalInst() || !Mop2->getOriginalInst()) {
    return false;
  }

  auto &DT = Context->getDominatorTree();
  auto &PDT = Context->getPostDominatorTree();
  return DT.dominates(Mop1->getOriginalInst(), Mop2->getOriginalInst()) ||
         PDT.dominates(Mop1->getOriginalInst(), Mop2->getOriginalInst());
}
