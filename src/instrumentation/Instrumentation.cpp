//===------------Instrumentation.cpp - TransformUtils Infrastructure ------===//
//
// This file is to provide some instrumentation utilities.
// These utilities usually come from the subsequent versions of LLVM, i.e., LLVM
// 15+.
//
//===----------------------------------------------------------------------===//
//
// This file defines the common initialization infrastructure for the
// Instrumentation library.
//
//===----------------------------------------------------------------------===//

#include "Instrumentation.h"
#include "Utils/Logging.h"
#include "Utils/Options.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "xsan_opt"

static cl::opt<bool> ClIgnoreRedundantInstrumentation(
    "ignore-redundant-instrumentation",
    cl::desc("Ignore redundant instrumentation"), cl::Hidden, cl::init(false));

/// Number of loop invariant checks relocated.
uint32_t NumInvChecksRelocated = 0;
/// Number of loop periodic checks combined.
uint32_t NumPeriodChecksCombined = 0;
namespace {
/// Diagnostic information for IR instrumentation reporting.
class DiagnosticInfoInstrumentation : public DiagnosticInfo {
  const Twine &Msg;

public:
  DiagnosticInfoInstrumentation(const Twine &DiagMsg,
                                DiagnosticSeverity Severity = DS_Warning)
      : DiagnosticInfo(DK_Linker, Severity), Msg(DiagMsg) {}
  void print(DiagnosticPrinter &DP) const override { DP << Msg; }
};
} // namespace

namespace __xsan {
/// Check if module has flag attached, if not add the flag.
bool checkIfAlreadyInstrumented(Module &M, StringRef Flag) {
  if (!M.getModuleFlag(Flag)) {
    M.addModuleFlag(Module::ModFlagBehavior::Override, Flag, 1);
    return false;
  }
  if (ClIgnoreRedundantInstrumentation)
    return true;
  std::string diagInfo =
      "Redundant instrumentation detected, with module flag: " +
      std::string(Flag);
  M.getContext().diagnose(
      DiagnosticInfoInstrumentation(diagInfo, DiagnosticSeverity::DS_Warning));
  return true;
}

constexpr char kDelegateMDKind[] = "xsan.delegate";

static unsigned DelegateMDKindID = 0;

void MarkAsDelegatedToXsan(Instruction &I) {
  if (IsDelegatedToXsan(I))
    return;
  auto &Ctx = I.getContext();
  if (!DelegateMDKindID)
    DelegateMDKindID = Ctx.getMDKindID(kDelegateMDKind);
  MDNode *N = MDNode::get(Ctx, None);
  I.setMetadata(DelegateMDKindID, N);
}

bool IsDelegatedToXsan(const Instruction &I) {
  if (!DelegateMDKindID)
    return false;
  return I.hasMetadata(DelegateMDKindID);
}

bool ShouldSkip(const Instruction &I) {
  return I.hasMetadata(LLVMContext::MD_nosanitize) || IsDelegatedToXsan(I);
}

/*
The following pattern is considered as a loop counting pattern.

header:
  %counter = phi [0, %preheader], [%inc, %latch]
  ...
latch:
  %inc = add %counter, 1
 */
static bool isLoopCountingPhi(PHINode *PN, BasicBlock *Predecessor,
                              BasicBlock *Latch, bool &ZeroBase) {
  // Check if there are exactly two incoming edges
  if (PN->getNumIncomingValues() != 2)
    return false;

  // Check if the type is integer
  if (!PN->getType()->isIntegerTy())
    return false;

  bool HasZeroOrOne = false;
  bool HasAddUp = false;
  // Traverse all incoming values of the PHI node
  for (unsigned Idx = 0; Idx < PN->getNumIncomingValues(); Idx++) {
    Value *Incoming = PN->getIncomingValue(Idx);
    BasicBlock *BB = PN->getIncomingBlock(Idx);

    // If incoming is a constant, check if it is 0
    if (ConstantInt *CI = dyn_cast<ConstantInt>(Incoming)) {
      if ((CI->isOne() || CI->isZero()) && BB == Predecessor) {
        HasZeroOrOne = true;
        ZeroBase = CI->isZero();
        continue;
      }
    } else if (AddOperator *IncomingInst = dyn_cast<AddOperator>(Incoming)) {
      // Otherwise, if incoming comes from an addition instruction, check if the
      // addition instruction is %phi + 1 Check if one operand is the current
      // PHI node and the other operand is the constant 1
      Value *Op0 = IncomingInst->getOperand(0);
      Value *Op1 = IncomingInst->getOperand(1);
      if ((Op0 == PN && isa<ConstantInt>(Op1) &&
           cast<ConstantInt>(Op1)->equalsInt(1)) ||
          (Op1 == PN && isa<ConstantInt>(Op0) &&
           cast<ConstantInt>(Op0)->equalsInt(1))) {
        HasAddUp = (BB == Latch);
      }
    }
  }
  return HasZeroOrOne && HasAddUp;
}

/*
 Currently, we only focus on simple loops:
 1. Single header, single predecessor, single latch
 2. Prefer to reuse the existing loop counter (PHI node) instead of creating a
    new one.
*/
static Value *getOrInsertLoopCounter(Loop *Loop, InstrumentationIRBuilder &IRB,
                                     const bool BeforeExiting) {
  BasicBlock *Header = Loop->getHeader(),
             *Predecessor = Loop->getLoopPredecessor(),
             *Latch = Loop->getLoopLatch();

  if (!Header || !Predecessor || !Latch)
    return nullptr; // If the loop does not have a header, it is unsafe to
                    // instrument

  // If the header has > 2 predecessors, it is unsafe to instrument
  if (!Header->hasNPredecessors(2))
    return nullptr;

  Value *Counter = nullptr;
  /* 1. Try to find an existing loop counter in the loop header (header) */
  for (Instruction &I : *Header) {
    // Only consider PHI nodes
    PHINode *Phi = dyn_cast<PHINode>(&I);
    if (!Phi)
      continue;

    bool ZeroBase = false;

    if (!isLoopCountingPhi(Phi, Predecessor, Latch, ZeroBase)) {
      continue;
    }

    Counter = Phi;

    // If not i64, cast it to i64
    if (Counter->getType() != IRB.getInt64Ty()) {
      Counter = IRB.CreateZExt(Counter, IRB.getInt64Ty());
    }

    if (ZeroBase && BeforeExiting) {
      // If MOP is before the exiting block, we should count it from 1.
      Counter = IRB.CreateAdd(Counter, IRB.getInt64(1), "add_one", true, true);
    } else if (!ZeroBase && !BeforeExiting) {
      // If MOP is after the exiting block, we should count it from 0.
      Counter = IRB.CreateSub(Counter, IRB.getInt64(1), "sub_one", true, true);
    }

    return Counter;
  }

  /* 2. If no existing counter was found, construct a new counter */

  // Create a new PHI node before the first non-PHI insertion point in the
  // header
  auto *OrigInsertPt = &*IRB.GetInsertPoint();
  IRB.SetInsertPoint(&*Header->getFirstInsertionPt());

  LLVMContext &Ctx = Header->getContext();

  // Preset number of incomings: 1 from preheader, plus one for each latch block
  SmallVector<BasicBlock *, 4> Latches;
  Loop->getLoopLatches(Latches);
  unsigned NumIncoming = 1 + Latches.size();
  PHINode *CounterPhi =
      IRB.CreatePHI(IRB.getInt64Ty(), NumIncoming, "loop.count");

  // Incoming value from preheader is
  CounterPhi->addIncoming(IRB.getInt64(BeforeExiting ? 1 : 0), Predecessor);
  // For each latch block, insert an addition instruction at its exit and add
  // the result to the PHI node
  for (BasicBlock *Latch : Latches) {
    IRBuilder<> LatchBuilder(Latch->getTerminator());
    // Generate: %inc = add counterPhi, 1
    Value *Incr = LatchBuilder.CreateAdd(CounterPhi, IRB.getInt64(1),
                                         "inc.loop.count", true, true);
    CounterPhi->addIncoming(Incr, Latch);
  }

  // Recover the original insertion point
  IRB.SetInsertPoint(OrigInsertPt);
  return CounterPhi;
}

/// Calculate End based on End = Beg + Step * Counter
/// Note: If MOP dominates Exiting (see @arg BeforeExiting), then Counter is
/// the number of backedge taken counts + 1, otherwise it is the number of
/// backedge taken counts
/// Note that, if Step is a negative constant, this function will adjust the
/// access range accordingly as follows (see handleRangeWithNegativeStep):
//  [Beg', End') = [End + |Step|, Beg + |Step|), i.e.,
/// [Start + Step * LoopCount + |Step|, Start + |Step|)
/// e.g., reads 4 bytes from 0x10, then ends at 0x04 with step -4
///   --> Step = -4, Start = 0x10, End = 0x04, LoopCount = 2
///   --> Beg  = 0x10 + (-4) * 2 + |-4| = 0x08
///       End  = 0x10 + |-4| = 0x14
///   --> We obtain the real access range [0x10, 0x14)
/// What's more, if Step is a negative variable, this function just expands
/// Beg and End as usual, and the relevant processing is delegated to the
/// caller or the runtime library.
static bool expandBegAndEnd(Loop *Loop, ScalarEvolution &SE,
                            SCEVExpander &Expander, const SCEV *Start,
                            const SCEV *Step, bool BeforeExiting,
                            InstrumentationIRBuilder &IRB, Value *&Beg,
                            Value *&End, Value *&StepVal) {
  // 1. Try to find an existing loop counter in the loop header (header)
  BasicBlock *Header = Loop->getHeader(),
             *Predecessor = Loop->getLoopPredecessor(),
             *Latch = Loop->getLoopLatch();

  if (!Header || !Predecessor || !Latch)
    return false; // If the loop does not have a header, it is unsafe to
                  // instrument

  Instruction *InsertPt = Predecessor->getTerminator();

  if (!Expander.isSafeToExpandAt(Start, InsertPt)) {
    // Unsafe to expand StartSCEV at header terminator, skip.
    return false;
  }

  const SCEVConstant *ConstStep = dyn_cast<SCEVConstant>(Step);
  ConstantInt *ConstStepVal = ConstStep ? ConstStep->getValue() : nullptr;
  bool IsConstStepNegative = ConstStepVal && ConstStepVal->isNegative();

  /// If Step is negative,  Start += |Step|, and the following
  /// End = Start + Step * LoopCount will be also added by |Step|
  if (IsConstStepNegative) {
    const auto *PosStep = SE.getNegativeSCEV(Step);
    Start = SE.getAddExpr(Start, PosStep);
  }

  /// TODO: figure out if it is Okay to insert code in Predecessor block
  Beg = Expander.expandCodeFor(Start, IRB.getInt8PtrTy(), InsertPt);
  StepVal = Expander.expandCodeFor(Step, IRB.getInt64Ty(), InsertPt);

  /// Does not consider step -1, which should be used as factor of term,
  /// negating the sign of offset.
  bool StepIsOne = ConstStepVal && ConstStepVal->isOne();

  const auto *BackedgeTakenCount = SE.getBackedgeTakenCount(Loop);
  if (!isa<SCEVCouldNotCompute>(BackedgeTakenCount)) {
    /* Try to get the loop's backedge taken count (loop invariant) via SCEV */

    // Note: backedge taken count does not include the first entry into the
    // header, so the actual trip count = backedge taken count + 1 Here we use
    // SCEVExpander to convert the SCEV expression to IR code.

    // Note: SCEVExpander requires DataLayout (DL) and a suitable insertion
    // point, such as using the loop preheader's terminal instruction
    // Theoretically, preheader is better, but a loop predecessor can also
    // work.
    const auto *IterCounter =
        BeforeExiting
            ? SE.getAddExpr(BackedgeTakenCount, SE.getConstant(IRB.getInt64(1)))
            : BackedgeTakenCount;
    const auto *Offset =
        (StepIsOne ? IterCounter : SE.getMulExpr(IterCounter, Step));
    const auto *EndSE = SE.getAddExpr(Start, Offset);

    End = Expander.expandCodeFor(EndSE, IRB.getInt8PtrTy(), InsertPt);

  } else {
    /* If the loop count isn't an invariant, manually insert a loop counter */
    Value *Counter = getOrInsertLoopCounter(Loop, IRB, BeforeExiting);
    if (!Counter) {
      return false;
    }

    /// If Step is negative, Beg = End + |Step| = Start + Step * (LoopCount - 1)
    Value *Offset = StepIsOne ? Counter : IRB.CreateMul(Counter, StepVal);
    End = IRB.CreateGEP(IRB.getInt8Ty(), Beg, {Offset});
  }

  if (IsConstStepNegative) {
    /// If Step is negative
    /// [Beg', End') = [End + |Step|, Beg + |Step|)
    std::swap(Beg, End);
  }

  return true;
}

LoopMopInstrumenter::LoopMopInstrumenter(Function &F,
                                         FunctionAnalysisManager &FAM,
                                         LoopOptLeval OptLevel)
    : F(F), FAM(FAM), LI(FAM.getResult<LoopAnalysis>(F)),
      DT(FAM.getResult<DominatorTreeAnalysis>(F)),
      PDT(FAM.getResult<PostDominatorTreeAnalysis>(F)),
      DL(F.getParent()->getDataLayout()), OptLevel(OptLevel),
      MopCollected(false) {
  Module &M = *F.getParent();
  LLVMContext &Ctx = M.getContext();
  IRBuilder<> IRB(Ctx);

  AttributeList Attr;
  Attr = Attr.addFnAttribute(Ctx, Attribute::NoUnwind);
  XsanRangeRead =
      M.getOrInsertFunction("__xsan_read_range", Attr, IRB.getVoidTy(),
                            IRB.getInt8PtrTy(), IRB.getInt8PtrTy());
  XsanRangeWrite =
      M.getOrInsertFunction("__xsan_write_range", Attr, IRB.getVoidTy(),
                            IRB.getInt8PtrTy(), IRB.getInt8PtrTy());
  for (size_t i = 0; i < kNumberOfAccessSizes; i++) {
    const unsigned ByteSize = 1U << i;
    std::string ByteSizeStr = utostr(ByteSize);

    XsanPeriodRead[i] = M.getOrInsertFunction(
        "__xsan_period_read" + ByteSizeStr, Attr, IRB.getVoidTy(),
        IRB.getInt8PtrTy(), IRB.getInt8PtrTy(), IRB.getInt64Ty());
    XsanPeriodWrite[i] = M.getOrInsertFunction(
        "__xsan_period_write" + ByteSizeStr, Attr, IRB.getVoidTy(),
        IRB.getInt8PtrTy(), IRB.getInt8PtrTy(), IRB.getInt64Ty());

    XsanRead[i] = M.getOrInsertFunction("__xsan_read" + ByteSizeStr, Attr,
                                        IRB.getVoidTy(), IRB.getInt8PtrTy());
    XsanWrite[i] = M.getOrInsertFunction("__xsan_write" + ByteSizeStr, Attr,
                                         IRB.getVoidTy(), IRB.getInt8PtrTy());
  }
}

void LoopMopInstrumenter::instrument() {
  if (F.isDeclaration() || F.empty())
    return;
  bool LoopChanged = false;
  switch (OptLevel) {
  case LoopOptLeval::CombineToRangeCheck:
    LoopChanged = combinePeriodicChecks(true);
    break;
  case LoopOptLeval::RelocateInvariantChecks:
    relocateInvariantChecks();
    break;
  case LoopOptLeval::CombinePeriodicChecks:
    LoopChanged = combinePeriodicChecks(false);
    break;
  case LoopOptLeval::Full:
    LoopChanged |= combinePeriodicChecks(false);
    // Invariant checks might insert new branches, so we need to run it later
    // than CombinePeriodicChecks.
    LoopChanged |= relocateInvariantChecks();
    break;
  case LoopOptLeval::NoOpt:
    return;
  }
}
SmallVectorImpl<LoopMopInstrumenter::LoopMop> &
LoopMopInstrumenter::getLoopMopCandidates() {
  if (!MopCollected) {
    collectLoopMopCandidates();
  }
  return LoopMopCandidates;
}

void LoopMopInstrumenter::collectLoopMopCandidates() {
  auto &DT = FAM.getResult<DominatorTreeAnalysis>(F);
  LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);

  // This only traverse the top-level loops, not nested loops.
  // See LI.getTopLevelLoops() for more details.
  for (BasicBlock &BB : F) {
    Loop *Loop = LI.getLoopFor(&BB);
    if (!Loop) {
      continue;
    }
    if (!isSimpleLoop(Loop)) {
      continue;
    }
    /// TODO: filter read before/after write
    for (Instruction &Inst : BB) {
      if (ShouldSkip(Inst))
        continue;
      /* 1. Filter out non-memory instructions */
      Value *Addr = nullptr;
      size_t MopSize = 0;
      bool InBranch = false;
      bool IsWrite = false;
      if (auto *SI = dyn_cast<StoreInst>(&Inst)) {
        Addr = SI->getPointerOperand();
        MopSize = DL.getTypeStoreSizeInBits(SI->getValueOperand()->getType());
        IsWrite = true;
      } else if (auto *LI = dyn_cast<LoadInst>(&Inst)) {
        Addr = LI->getPointerOperand();
        MopSize = DL.getTypeStoreSizeInBits(LI->getType());
        IsWrite = false;
      }

      if (!Addr)
        continue;

      /* 2. Filter out non-regular memory instructions */
      /// Only consider 8, 16, 32, 64, 128 bit access
      if (MopSize != 8 && MopSize != 16 && MopSize != 32 && MopSize != 64 &&
          MopSize != 128) {
        continue;
      }

      MopSize /= 8;

      auto *LoopLatch = Loop->getLoopLatch();
      assert(LoopLatch && "Loop must have single latch");
      if (!DT.dominates(Inst.getParent(), LoopLatch)) {
        // If Inst does not dominate the loop latch, it's within a loop branch
        InBranch = true;
      }

      /*
        struct LoopMop {
          Instruction *Mop;
          Value *Address;
          Loop *Loop;
          size_t MopSize;
          bool InBranch;
          bool IsWrite;
        };
      */
      LoopMop Candidate = {&Inst, Addr, Loop, MopSize, InBranch, IsWrite};
      LoopMopCandidates.push_back(Candidate);
    }
  }
  MopCollected = true;
}

bool LoopMopInstrumenter::isSimpleLoop(const Loop *Loop) {
  if (!Loop)
    return false;
  if (SimpleLoops.contains(Loop))
    return true;
  if (ComplexLoops.contains(Loop))
    return false;

  /// TODO: relax this condition
  /// ONE latch, ONE predecessor, ONE header, ONE exiting block, ONE exit block
  if (!Loop->getHeader() || !Loop->getLoopPredecessor() ||
      !Loop->getLoopLatch() || !Loop->getExitBlock() ||
      !Loop->getExitingBlock()) {
    ComplexLoops.insert(Loop);
    return false;
  }

  // Loop that contains atomic instructions or calls is not simple.
  // Notaly, a loop with a pure function call that does not write to
  // memory is also considered as a simple loop.
  for (const BasicBlock *BB : Loop->getBlocks()) {
    for (const Instruction &I : *BB) {
      if (ShouldSkip(I))
        continue;
      if (!I.mayWriteToMemory())
        continue;
      /// TODO: should consider Atomic for ASan ?
      if (I.isAtomic() || isa<CallBase>(&I)) {
        ComplexLoops.insert(Loop);
        return false;
      }
    }
  }
  SimpleLoops.insert(Loop);
  return true;
}

/*
   while (cond) {
      CHECK(ptr)
      *ptr++ = val;
   }

-->

   ptr_init = ptr;
   loop_count = 0;
   while (cond) {
      *ptr++ = val;
      loop_count++;
   }
   CHECK(ptr_init, ptr_init + loop_count * sizeof(val))

Currently only targets the simplest loops, i.e.:
 1. Single header, single predecessor, single latch, single exit
 2. Loads/stores not within branches.
 3. Canonical loops, i.e., well-formed loops.
TODO: Perhaps we can relax these conditions in the future. For example, multiple
exits, multiple exits, multiple latches (in which case each MOP needs to
maintain a corresponding counter)
 */
bool LoopMopInstrumenter::combinePeriodicChecks(bool RangeAccessOnly) {
  bool LoopChanged = false;
  // Get SCEV analysis result
  auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);

  SCEVExpander Expander(SE, DL, "expander");

  // Those MOPs in the same BB are guaranteed to be adjacent and ordered by
  // their IR order.
  for (LoopMop &Mop : getLoopMopCandidates()) {
    auto &[Inst, Addr, Loop, MopSize, InBranch, IsWrite] = Mop;
    if (InBranch) {
      // Periodic MOPs combination is not supported in branches currently, skip.
      continue;
    }

    /* 1. Filter out non-periodic MOPs */

    auto *PtrSCEV = SE.getSCEV(Addr);
    if (!PtrSCEV)
      continue;

    /// TODO: transfer SCEVAddExpr(SCEVAddRecExpr) to
    /// SCEVAddRecExpr(SCEVAddExpr)
    const auto *AR = dyn_cast<SCEVAddRecExpr>(PtrSCEV);
    if (!AR)
      continue;

    const auto *Step = AR->getStepRecurrence(SE);

    // ----------- Extract Loop-Invariant step -----------
    const SCEVConstant *ConstStep = dyn_cast<SCEVConstant>(Step);
    bool IsRangeAccess;
    if (!ConstStep) {
      // If not a constant, check if it is a Loop-Invariant
      if (!SE.isLoopInvariant(Step, Loop)) {
        continue; // Skip if not a loop invariant
      }
      // If Step is not a constant, do not use range access to model.
      IsRangeAccess = false;
    } else {
      ConstantInt *StepVal = ConstStep->getValue();
      // Negative step is considered as positive step
      size_t StepValInt = StepVal->getSExtValue() < 0 ? -StepVal->getSExtValue()
                                                      : StepVal->getSExtValue();
      IsRangeAccess = (StepValInt == MopSize);
    }

    if (RangeAccessOnly && !IsRangeAccess) {
      // If step is not MopSize, it's not full range access, skip
      continue;
    }

    auto *ExitBlock = Loop->getUniqueExitBlock();
    auto *Exiting = Loop->getExitingBlock();

    /* 2. Combine periodic MOPs into a single range check */

    // If dominates Exiting, need to add 1 to the counter, otherwise no need
    // to add
    bool IsMopBeforeExiting = DT.dominates(Inst->getParent(), Exiting);

    // The only predecessor of exit should be the exiting block.
    if (ExitBlock->getUniquePredecessor() != Exiting) {
      // Update ExitBlock
      ExitBlock =
          SplitEdge(Exiting, ExitBlock, &DT, &LI, nullptr, "xsan.loop.exit");
      LoopChanged = true;
    }

    InstrumentationIRBuilder IRB(&*ExitBlock->getFirstInsertionPt());

    const auto *Start = AR->getStart();
    Value *Beg, *End, *StepVal;
    if (!expandBegAndEnd(Loop, SE, Expander, Start, Step, IsMopBeforeExiting,
                         IRB, Beg, End, StepVal)) {
      // If failed to expand, skip.
      continue;
    }

    if (IsRangeAccess) {
      // void __xsan_read_range(const void *beg, const void *end) {
      // void __xsan_write_range(const void *beg, const void *end) {
      IRB.CreateCall(IsWrite ? XsanRangeWrite : XsanRangeRead, {Beg, End});
    } else {
      size_t Idx = countTrailingZeros(MopSize);
      // __xsan_period_readX(const void *beg, const void *end, size_t step)
      // __xsan_period_writeX(const void *beg, const void *end, size_t step)
      IRB.CreateCall(IsWrite ? XsanPeriodWrite[Idx] : XsanPeriodRead[Idx],
                     {Beg, End, StepVal});
    }

    MarkAsDelegatedToXsan(*Inst);
    NumPeriodChecksCombined++;
  }

  if (options::ClDebug) {
    Log.setFunction(F.getName());
    Log.addLog("[LoopOpt] #{Combined Loop Mops}", NumPeriodChecksCombined);
  }

  return LoopChanged;
}

// Instrument a flag variable to indicate if the MOP is executed in loop.
// Returns the new insert point pointing to the new branch.
// --- Before instrumentation ---
// Exit:
//   ...
//
// --- After instrumentation ---
// Exit:
//   indicator = ...; // new instruction
//   if (indicator) {
//     --> insert here
//   }
static Instruction *instrumentIndicator(Instruction *Inst,
                                        BasicBlock *ExitBlock,
                                        DominatorTree &DT, LoopInfo &LI) {
  /// TODO: do not rely on the optimization, just instrument a GOOD IR with
  /// PHINode.

  /* Insert code to get the indicator indicating if the MOP is executed in loop
   */

  Function *F = Inst->getParent()->getParent();
  BasicBlock &Entry = F->getEntryBlock();

  InstrumentationIRBuilder IRB(&*Entry.getFirstInsertionPt());
  auto *IndicatorAlloc =
      IRB.CreateAlloca(IRB.getInt1Ty(), nullptr, "indicator");

  IRB.SetInsertPoint(Inst);
  IRB.CreateStore(IRB.getTrue(), IndicatorAlloc);

  IRB.SetInsertPoint(&*ExitBlock->getFirstInsertionPt());
  auto *Indicator = IRB.CreateLoad(IRB.getInt1Ty(), IndicatorAlloc);

  /* Insert new branch according to the value of indicator */
  Instruction *EqTrue =
      (Instruction *)IRB.CreateICmpEQ(Indicator, IRB.getTrue());
  /*
    If DT (DominatorTree) is used to be updated in other places, we cannot use
    DTU (DomTreeUpdater) here, because we need to consider the consistence with
    the DT. As if DTU is used, the new DT should be obtained by
    DTU.getDomTree().
  */
  auto *Term = SplitBlockAndInsertIfThen(EqTrue, EqTrue->getNextNode(), false,
                                         nullptr, &DT, &LI);

  return Term;
}

bool LoopMopInstrumenter::relocateInvariantChecks() {
  bool LoopChanged = false;
  Instruction *LastInertPt = nullptr;
  BasicBlock *LastBB = nullptr;
  for (LoopMop &Mop : getLoopMopCandidates()) {
    auto &[Inst, Addr, L, MopSize, InBranch, IsWrite] = Mop;
    if (!L->isLoopInvariant(Addr)) {
      // Skip if Addr is not a loop invariant
      continue;
    }
    // Top loop to maintain the invarianty of Addr
    Loop *TopL = L, *ParentL = L->getParentLoop();
    while (ParentL && ParentL->isLoopInvariant(Addr) &&
           /// TODO: for ASan, simple loop is not necessary, we need to relax
           /// this condition
           isSimpleLoop(ParentL)) {
      TopL = ParentL;
      ParentL = TopL->getParentLoop();
    }

    auto *Preheader = TopL->getLoopPreheader();
    // We should alse consider the branch from preheader to MOP if InBranch is
    // false.
    bool IsInBranch =
        InBranch ? InBranch : !PDT.dominates(Inst->getParent(), Preheader);

    /* Get the insert point for invariant check relocating */
    Instruction *InsertPt;
    bool SameBBWithLast = LastBB && LastBB == Inst->getParent();
    bool SinkToExit = IsInBranch || !Preheader;
    if (SameBBWithLast) {
      // Just reuse the last insert point
      InsertPt = LastInertPt;
    } else if (!SinkToExit) {
      // If has preheader and not in branch, insert at the terminator of the
      // preheader.
      InsertPt = Preheader->getTerminator();
    } else {
      // If no preheader, sinstrument on the exit.
      auto *ExitBlock = TopL->getUniqueExitBlock();
      auto *Exiting = TopL->getExitingBlock();

      // The only predecessor of exit should be the exiting block.
      if (ExitBlock->getUniquePredecessor() != Exiting) {
        // Update ExitBlock
        // ExitBlock = splitNewExitBlock(ExitBlock, Exiting);
        ExitBlock =
            SplitEdge(Exiting, ExitBlock, &DT, &LI, nullptr, "xsan.loop.exit");

        LoopChanged = true;
      }
      if (IsInBranch) {
        // Insert indicator to indicate if the MOP is executed in loop.
        InsertPt = instrumentIndicator(Inst, ExitBlock, DT, LI);
        if (!InsertPt) {
          continue;
        }
      } else {
        // If not in branch, insert at the beginning of the exit block.
        InsertPt = &*ExitBlock->getFirstInsertionPt();
      }
    }

    size_t Idx = countTrailingZeros(MopSize);

    InstrumentationIRBuilder IRB(InsertPt);
    // __xsan_readX(const void *beg)
    // __xsan_writeX(const void *beg)
    IRB.CreateCall(IsWrite ? XsanWrite[Idx] : XsanRead[Idx], {Addr});
    MarkAsDelegatedToXsan(*Inst);
    LastInertPt = InsertPt;
    LastBB = Inst->getParent();
    NumInvChecksRelocated++;
  }

  if (options::ClDebug) {
    Log.setFunction(F.getName());
    Log.addLog("[LoopOpt] #{Relocated Loop Mops}", NumInvChecksRelocated);
  }

  return LoopChanged;
}
} // namespace __xsan