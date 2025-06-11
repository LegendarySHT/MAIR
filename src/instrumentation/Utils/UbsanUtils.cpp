#include "UbsanUtils.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"

using namespace llvm;

namespace {
constexpr const char *UbsanInterfacePrefix = "__ubsan_";

}

namespace __xsan {

// E.g., __ubsan_handle_add_overflow
bool isUbsanInterfaceCall(const Instruction &I) {
  if (!isa<CallBase>(I)) {
    return false;
  }
  if (!isNoSanitize(I)) {
    return false;
  }
  auto &CB = cast<CallBase>(I);
  auto *CalledFunction = CB.getCalledFunction();
  return CalledFunction &&
         CalledFunction->getName().startswith(UbsanInterfacePrefix);
}

/*
Match with the following pattern:
```
  %2 = tail call { i32, i1 } @llvm.sadd.with.overflow.i32(i32 %0, i32 1),
!nosanitize !5 %3 = extractvalue { i32, i1 } %2, 1, !nosanitize !5 br i1 %3,
label %4, label %6, !prof !6, !nosanitize !5

4:                                                ; preds = %1
  %5 = zext i32 %0 to i64, !nosanitize !5
  tail call void @__ubsan_handle_add_overflow(ptr nonnull @1, i64 %5, i64 1) #3,
!nosanitize !5 br label %6, !nosanitize !5

6:                                                ; preds = %4, %1
  %7 = extractvalue { i32, i1 } %2, 0, !nosanitize !5
  ret i32 %7
```
*/
bool isCheckedIntegerOverflowIntrinsicCall(llvm::Instruction &CB, bool strict) {
  if (!isNoSanitize(CB)) {
    return false;
  }

  // Check if the call is a checked integer overflow intrinsic call.
  // E.g, llvm.sadd.with.overflow.i32, llvm.uadd.with.overflow.i32, etc.
  // UBSan/Integer Sanitizer will instrument these calls.
  IntrinsicInst *II = dyn_cast<IntrinsicInst>(&CB);
  if (!II) {
    return false;
  }
  const Intrinsic::ID ValidIDs[] = {
      Intrinsic::sadd_with_overflow, Intrinsic::uadd_with_overflow,
      Intrinsic::ssub_with_overflow, Intrinsic::usub_with_overflow};
  if (!llvm::is_contained(ValidIDs, II->getIntrinsicID())) {
    return false;
  }

  // Check if the call affect conditional branch.
  BranchInst *BI = dyn_cast<BranchInst>(CB.getParent()->getTerminator());
  if (!BI || !BI->isConditional()) {
    return false;
  }

  // Check if the branch instruction has a successor block that is a UBSan
  // fallback block.
  if (!any_of(BI->successors(), isUbsanFallbackBlock)) {
    return false;
  }

  if (!strict) {
    return true;
  }

  // Track the use-def chain of the branch instruction.
  Value *V = BI->getCondition();
  const unsigned MaxLookup = 2;
  for (unsigned Count = 0; Count < MaxLookup; ++Count) {
    // If V is a xor instruction, we need to check the two operands.
    if (auto *Xor = dyn_cast<BinaryOperator>(V)) {
      if (Xor->getOpcode() != BinaryOperator::Xor) {
        return false;
      }
      // In O0 :  xor i1 %x, true
      V = Xor->getOperand(0);
    }
    // Match extractvalue instruction.
    else if (auto *ExtractValue = dyn_cast<ExtractValueInst>(V)) {
      if (ExtractValue->getNumIndices() != 1) {
        return false;
      }
      unsigned Index = ExtractValue->getIndices()[0];
      if (Index != 1) {
        return false;
      }
      V = ExtractValue->getAggregateOperand();
    }
  }
  return V == &CB;
}

bool isUbsanFallbackBlock(const llvm::BasicBlock &BB) {
  if (!isNoSanitize(*BB.getTerminator())) {
    return false;
  }
  return any_of(BB, isUbsanInterfaceCall);
}

} // namespace __xsan