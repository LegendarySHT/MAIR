//===------------Instrumentation.h - TransformUtils Infrastructure --------===//
//
// This file is to provide some instrumentation utilities APIs.
// These APIs usually come from the subsequent versions of LLVM, i.e., LLVM 15+.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Dominators.h"
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

/// Utils class to check if a value is loop invariant.
/// Note that this class should be used at those simple loops.
class LoopInvariantChecker {
public:
  LoopInvariantChecker(const DominatorTree &DT);
  bool hasUBSan() const { return UBSanExists; }
  bool isLoopInvariant(Value *V, const Loop *L) const;
  bool isLoopInvariant(ScalarEvolution &SE, const SCEV *S, const Loop *L) const;

private:
  bool isLoopInvariant(const SCEV *S, const Loop *L) const;

  bool UBSanExists;
  const DominatorTree &DT;
};

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
private:
  using DupVec = SmallVector<Instruction *, 4>;
  struct LoopMop {
    Instruction *Mop;
    Value *Address;
    Loop *Loop;
    size_t MopSize;
    /// Records the MOPs redundant to this MOPs.
    DupVec DupTo;
    /* If true, the Mop might not be executed in every iteration of the loop. */
    bool InBranch;
    bool IsWrite;
  };

public:
  static LoopMopInstrumenter create(Function &F, FunctionAnalysisManager &FAM,
                                    LoopOptLeval OptLevel);

  /*
   1. Relocate invariant checks: if a loop invariant is used in the loop, sink
      it out of the loop.
   2. Combine preiodic checks: combine temporal adjacent periodic checks into a
      single check.
   */
  void instrument();

private:
  LoopMopInstrumenter(Function &F, FunctionAnalysisManager &FAM,
                      LoopOptLeval OptLevel);

  // A simple loop is currently defined as a loop with
  // 1. single header, exit, latch, exiting. predecessor.
  // 2. conatains no atomic instructions and
  //    no function calls (apart from pure readonly function)
  /// TODO: for ASan, such restrictions can be relaxed.
  /// TODO: if the MOP is ahead of all exiting, multiple exitings
  ///       do not affect the loop optimization.
  bool isSimpleLoop(const Loop *L);

  /// Filter out those obvious duplicate MOPs in the same BB,
  /// being formalized as follows
  /// For ∀m1 ≠ m2 ∈ MOPs, m1 is a duplicate of m2 if
  ///     1. m1.addr = m2.addr
  ///     2. m1.type = m2.type ∨ m1.type = read
  ///     3. ∀ i ∈ (m1.loc, m2.loc), isNotCall(i)
  /// `collectLoopMopCandidates` is the caller of this function,
  ///  guaranting that the third condition holding.
  void filterAndAddMops(SmallVectorImpl<LoopMop> &MOPs);
  // Collect loop mop candidates, i.e., MOPs that in SIMPLE loops.
  // Those MOPs in the same BB are guaranteed to be adjacent and ordered by
  // their IR order.
  void collectLoopMopCandidates();
  SmallVectorImpl<LoopMop> &getLoopMopCandidates();
  // hoist / sink the checks if their checked addresses are loop invariant
  bool relocateInvariantChecks();
  // Induction-based Instrumentation
  bool combinePeriodicChecks(bool RangeAccessOnly = true);

  LoopOptLeval OptLevel;
  Function &F;
  FunctionAnalysisManager &FAM;
  LoopInfo &LI;
  DominatorTree &DT;
  PostDominatorTree &PDT;
  const DataLayout &DL;

  SmallPtrSet<const Loop *, 16> SimpleLoops;
  SmallPtrSet<const Loop *, 16> ComplexLoops;
  SmallVector<LoopMop, 16> LoopMopCandidates;
  bool MopCollected;

  FunctionCallee XsanRangeRead;
  FunctionCallee XsanRangeWrite;

  // Accesses sizes are powers of two: 1, 2, 4, 8, 16.
  static const size_t kNumberOfAccessSizes = 5;
  FunctionCallee XsanPeriodRead[kNumberOfAccessSizes];
  FunctionCallee XsanPeriodWrite[kNumberOfAccessSizes];
  FunctionCallee XsanRead[kNumberOfAccessSizes];
  FunctionCallee XsanWrite[kNumberOfAccessSizes];
  const LoopInvariantChecker LIC;
};

} // namespace __xsan