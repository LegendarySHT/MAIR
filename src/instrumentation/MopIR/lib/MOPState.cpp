#include "../include/MOPState.h"
#include <optional>
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Operator.h"

using namespace llvm;
using namespace __xsan::MopIR;

namespace {
static std::optional<TypeSize> getPointerSize(const Value *V,
                                              const DataLayout &DL,
                                              const TargetLibraryInfo &TLI,
                                              const Function *F) {
  uint64_t Size;
  ObjectSizeOpts Opts;
  Opts.NullIsUnknownSize = NullPointerIsDefined(F);

  if (getObjectSize(V, Size, DL, &TLI, Opts))
    return TypeSize::getFixed(Size);
  return std::nullopt;
}
} // namespace

MOPState::MOPState(Function &F, AAResults &AA, DominatorTree &DT,
                   PostDominatorTree &PDT, const TargetLibraryInfo &TLI,
                   LoopInfo &LI, ScalarEvolution &SE)
    : F(F), AA(AA), EI(DT, LI, EphValues), BatchAA(AA, &EI), DT(DT), PDT(PDT),
        TLI(TLI), DL(F.getParent()->getDataLayout()), LI(LI), SE(SE) {
      // 保守：不特殊处理不可约环，按可约处理
      ContainsIrreducibleLoops = false;
}

bool MOPState::isMaskedStoreOverwrite(const Instruction *KillingI,
                                      const Instruction *DeadI) {
  const auto *KillingII = dyn_cast<IntrinsicInst>(KillingI);
  const auto *DeadII = dyn_cast<IntrinsicInst>(DeadI);
  if (!KillingII || !DeadII)
    return false;
  if (KillingII->getIntrinsicID() != Intrinsic::masked_store ||
      DeadII->getIntrinsicID() != Intrinsic::masked_store)
    return false;
  Value *KillingPtr = KillingII->getArgOperand(1)->stripPointerCasts();
  Value *DeadPtr = DeadII->getArgOperand(1)->stripPointerCasts();
  if (KillingPtr != DeadPtr && !BatchAA.isMustAlias(KillingPtr, DeadPtr))
    return false;
  // TODO: 更精细地检查 mask 覆盖关系；先要求完全相同。
  if (KillingII->getArgOperand(3) != DeadII->getArgOperand(3))
    return false;
  return true;
}

bool MOPState::isGuaranteedLoopInvariant(const Value *Ptr) {
  Ptr = Ptr->stripPointerCasts();
  if (auto *GEP = dyn_cast<GEPOperator>(Ptr))
    if (GEP->hasAllConstantIndices())
      Ptr = GEP->getPointerOperand()->stripPointerCasts();

  if (const auto *I = dyn_cast<Instruction>(Ptr)) {
    if (I->getParent()->isEntryBlock())
      return true;
    const Loop *L = LI.getLoopFor(I->getParent());
    if (!ContainsIrreducibleLoops && !L)
      return true;

    // 额外：尝试用 SCEV 判断在当前循环 L 内是否不变
    if (L && SE.isSCEVable(Ptr->getType())) {
      const SCEV *S = SE.getSCEV(const_cast<Value *>(Ptr));
      if (SE.isLoopInvariant(S, L))
        return true;
    }
    return false;
  }
  return true;
}

bool MOPState::isGuaranteedLoopIndependent(const Instruction *Current,
                                           const Instruction *KillingDef,
                                           const MemoryLocation &CurrentLoc) {
  if (Current->getParent() == KillingDef->getParent())
    return true;
  const Loop *CurrentLI = LI.getLoopFor(Current->getParent());
  if (!ContainsIrreducibleLoops && CurrentLI &&
      CurrentLI == LI.getLoopFor(KillingDef->getParent()))
    return true;
  return isGuaranteedLoopInvariant(CurrentLoc.Ptr);
}

LocationSize MOPState::strengthenLocationSize(const Instruction *I,
                                              LocationSize Size) const {
  if (const auto *CB = dyn_cast<CallBase>(I)) {
    LibFunc Fnc;
    if (TLI.getLibFunc(*CB, Fnc) && TLI.has(Fnc) &&
        (Fnc == LibFunc_memset_chk || Fnc == LibFunc_memcpy_chk)) {
      if (const auto *Len = dyn_cast<ConstantInt>(CB->getArgOperand(2)))
        return LocationSize::precise(Len->getZExtValue());
    }
  }
  return Size;
}

bool MOPState::isAccessRangeContains(const Instruction *KillingI,
                                     const Instruction *DeadI,
                                     const MemoryLocation &KillingLoc,
                                     const MemoryLocation &DeadLoc,
                                     int64_t &KillingOff, int64_t &DeadOff) {
  // 限制在循环无关的场景下使用 AA 作区间覆盖判断
  if (!isGuaranteedLoopIndependent(DeadI, KillingI, DeadLoc))
    return false;

  LocationSize KillingLocSize = strengthenLocationSize(KillingI, KillingLoc.Size);
  const Value *DeadPtr = DeadLoc.Ptr->stripPointerCasts();
  const Value *KillingPtr = KillingLoc.Ptr->stripPointerCasts();
  const Value *DeadUndObj = getUnderlyingObject(DeadPtr);
  const Value *KillingUndObj = getUnderlyingObject(KillingPtr);

  // 杀死方完整覆盖对象
  if (DeadUndObj == KillingUndObj && KillingLocSize.isPrecise() &&
      isIdentifiedObject(KillingUndObj)) {
    if (auto ObjSz = getPointerSize(KillingUndObj, DL, TLI, &F))
      if (*ObjSz == KillingLocSize.getValue())
        return true;
  }

  // 处理不精确大小：尝试 mem* 一致长度 + MustAlias；masked_store 完全覆盖
  if (!KillingLoc.Size.isPrecise() || !DeadLoc.Size.isPrecise()) {
    if (const auto *KM = dyn_cast<MemIntrinsic>(KillingI))
      if (const auto *DM = dyn_cast<MemIntrinsic>(DeadI)) {
        if (KM->getLength() == DM->getLength() &&
            BatchAA.isMustAlias(DeadLoc, KillingLoc))
          return true;
      }
    return isMaskedStoreOverwrite(KillingI, DeadI);
  }

  const uint64_t KillingSize = KillingLoc.Size.getValue();
  const uint64_t DeadSize = DeadLoc.Size.getValue();

  // AA 查询
  AliasResult AAR = BatchAA.alias(KillingLoc, DeadLoc);
  if (AAR == AliasResult::MustAlias) {
    if (KillingLocSize.isPrecise()) {
      if (KillingLocSize.getValue() >= DeadSize)
        return true;
    } else if (KillingSize >= DeadSize) {
      return true;
    }
  }

  if (AAR == AliasResult::PartialAlias && AAR.hasOffset()) {
    int32_t Off = AAR.getOffset();
    if (Off >= 0 && (uint64_t)Off + DeadSize <= KillingSize)
      return true;
  }

  if (DeadUndObj != KillingUndObj)
    return false;

  DeadOff = 0;
  KillingOff = 0;
  const Value *DeadBasePtr =
      GetPointerBaseWithConstantOffset(DeadPtr, DeadOff, DL);
  const Value *KillingBasePtr =
      GetPointerBaseWithConstantOffset(KillingPtr, KillingOff, DL);
  if (DeadBasePtr != KillingBasePtr)
    return false;

  // 区间包含判断
  if (DeadOff >= KillingOff) {
    if (uint64_t(DeadOff - KillingOff) + DeadSize <= KillingSize)
      return true;
  }
  return false;
}
