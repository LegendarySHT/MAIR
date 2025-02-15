//===------------Instrumentation.h - TransformUtils Infrastructure --------===//
//
// This file is to provide some instrumentation utilities APIs.
// These APIs usually come from the subsequent versions of LLVM, i.e., LLVM 15+.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"

namespace __xsan {
using namespace llvm;

/// Check if module has flag attached, if not add the flag.
bool checkIfAlreadyInstrumented(Module &M, StringRef Flag);

// Use to ensure the inserted instrumentation has a !nosanitize metadata.
struct InstrumentationIRBuilder : public IRBuilder<> {
  static void ensureDebugInfo(IRBuilder<> &IRB, const Function &F) {
    if (IRB.getCurrentDebugLocation())
      return;
    if (DISubprogram *SP = F.getSubprogram())
      IRB.SetCurrentDebugLocation(DILocation::get(SP->getContext(), 0, 0, SP));
  }

  void ensureNoSanitize() {
    // Because `AddOrRemoveMetadataToCopy` is private, we can only use it by the
    // delegation of `CollectMetadataToCopy`.
    // Therefore, we create a temporary instruction with the desired !nosanitize
    // metadata.
    llvm::ReturnInst *I = llvm::ReturnInst::Create(getContext());
    I->setMetadata(LLVMContext::MD_nosanitize,
                   MDNode::get(I->getContext(), None));
    // Every instructions created by this IRBuilder will have this metadata.
    this->CollectMetadataToCopy(I, {LLVMContext::MD_nosanitize});
    // this->AddOrRemoveMetadataToCopy(LLVMContext::MD_nosanitize,MDNode::get(Context,
    // None));

    // We don't need the temporary instruction anymore.
    delete I;
  }

  explicit InstrumentationIRBuilder(LLVMContext &C) : IRBuilder<>(C) {
    ensureNoSanitize();
  }
  explicit InstrumentationIRBuilder(Instruction *IP) : IRBuilder<>(IP) {
    ensureDebugInfo(*this, *IP->getFunction());
    ensureNoSanitize();
  }

  InstrumentationIRBuilder(BasicBlock *TheBB, BasicBlock::iterator IP)
      : IRBuilder<>(TheBB, IP) {
    ensureDebugInfo(*this, *TheBB->getParent());
    ensureNoSanitize();
  }
};

/// Mark an instruction as delegated to XSan.
void MarkAsDelegatedToXsan(Instruction &I);

/// Check if an instruction is delegated to XSan.
bool IsDelegatedToXsan(const Instruction &I);

bool ShouldSkip(const Instruction &I);

enum class LoopOptLeval {
  NoOpt,                   /* No optimization */
  RelocateInvariantChecks, /* Relocate invariant checks */
  CombineToRangeCheck,     /* Combine periodic checks to range checks*/
  CombinePeriodicChecks,   /* Combine periodic checks, including range checks */
  Full                     /* RelocateInvariantChecks + CombinePeriodicChecks*/
};

/*
 Optimize in simple canonical loop, i.e., no branching, no function call:
  1. Relocate invariant checks: if a loop invariant is used in the loop, sink
      it out of the loop.
  2. Combine preiodic checks: combine temporal adjacent periodic checks into a
      single check.
*/
class LoopMopInstrumenter {
public:
  LoopMopInstrumenter(Function &F, FunctionAnalysisManager &FAM, LoopOptLeval OptLevel);
  /*
   1. Relocate invariant checks: if a loop invariant is used in the loop, sink
      it out of the loop.
   2. Combine preiodic checks: combine temporal adjacent periodic checks into a
      single check.
   */
  void instrument();
private:
  bool isSimpleLoop(const Loop *L);
  // hoist / sink the checks if their checked addresses are loop invariant
  void relocateInvariantChecks();
  // Induction-based Instrumentation
  bool combinePeriodicChecks(bool RangeAccessOnly = true);

  LoopOptLeval OptLevel;
  Function &F;
  FunctionAnalysisManager &FAM;
  SmallPtrSet<const Loop *, 16> SimpleLoops;
  SmallPtrSet<const Loop *, 16> ComplexLoops;

  FunctionCallee XsanRangeRead;
  FunctionCallee XsanRangeWrite;

  // Accesses sizes are powers of two: 1, 2, 4, 8, 16.
  static const size_t kNumberOfAccessSizes = 5;
  FunctionCallee XsanPeriodRead[kNumberOfAccessSizes];
  FunctionCallee XsanPeriodWrite[kNumberOfAccessSizes];
};

} // namespace __xsan