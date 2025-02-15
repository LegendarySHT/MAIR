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
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"

using namespace llvm;

static cl::opt<bool> ClIgnoreRedundantInstrumentation(
    "ignore-redundant-instrumentation",
    cl::desc("Ignore redundant instrumentation"), cl::Hidden, cl::init(false));
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
static Value *getOrInsertLoopCounter(Loop *L, InstrumentationIRBuilder &IRB,
                                     const bool BeforeExiting) {
  BasicBlock *Header = L->getHeader(), *Predecessor = L->getLoopPredecessor(),
             *Latch = L->getLoopLatch();

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
  auto OrigInsertPt = IRB.GetInsertPoint();
  IRB.SetInsertPoint(&*Header->getFirstInsertionPt());

  LLVMContext &Ctx = Header->getContext();

  // Preset number of incomings: 1 from preheader, plus one for each latch block
  SmallVector<BasicBlock *, 4> Latches;
  L->getLoopLatches(Latches);
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
  return CounterPhi;
}

/// Calculate End based on End = Beg + Step * Counter
/// Note: If MOP dominates Exiting (see @arg BeforeExiting), then Counter is
/// the number of backedge taken counts + 1, otherwise it is the number of
/// backedge taken counts
static bool expandBegAndEnd(Loop *L, ScalarEvolution &SE,
                            SCEVExpander &Expander, const SCEV *Start,
                            const SCEVConstant *Step, bool BeforeExiting,
                            InstrumentationIRBuilder &IRB, Value *&Beg,
                            Value *&End) {
  // 1. Try to find an existing loop counter in the loop header (header)
  BasicBlock *Header = L->getHeader(), *Predecessor = L->getLoopPredecessor(),
             *Latch = L->getLoopLatch();

  if (!Header || !Predecessor || !Latch)
    return false; // If the loop does not have a header, it is unsafe to
                  // instrument

  Instruction *InsertPt = Predecessor->getTerminator();

  if (!Expander.isSafeToExpandAt(Start, InsertPt)) {
    // Unsafe to expand StartSCEV at header terminator, skip.
    return false;
  }

  /// TODO: figure out if it is Okay to insert code in Predecessor block
  Beg = Expander.expandCodeFor(Start, IRB.getInt8PtrTy(), InsertPt);

  bool StepIsOne = Step->getAPInt() == 1;

  /* 1. Try to get the loop's backedge taken count (loop invariant) through SCEV
   */
  const auto *BackedgeTakenCount = SE.getBackedgeTakenCount(L);
  if (!isa<SCEVCouldNotCompute>(BackedgeTakenCount)) {
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

    return true;
  }

  /* 2. If the loop count is not an invariant, manually insert a loop counter */
  Value *Counter = getOrInsertLoopCounter(L, IRB, BeforeExiting);
  if (!Counter) {
    return false;
  }

  Value *Offset =
      StepIsOne ? Counter : IRB.CreateMul(Counter, Step->getValue());
  End = IRB.CreateGEP(IRB.getInt8Ty(), Beg, {Offset});

  return true;
}

/// Exiting --> Exit
/// ===>
/// Exiting --> NewExit --> Exit
/// @return NewExit : the new exit block
static BasicBlock *splitNewExitBlock(BasicBlock *ExitBlock,
                                     BasicBlock *Exiting) {
  Function &F = *ExitBlock->getParent();
  auto *NewExitBlock =
      BasicBlock::Create(F.getContext(), "xsan.loop.exit", &F, ExitBlock);
  BranchInst::Create(ExitBlock, NewExitBlock)
      ->setMetadata(LLVMContext::MD_nosanitize,
                    MDNode::get(F.getContext(), None));
  /// TODO: support multiple exitings
  // SmallVector<BasicBlock *, 4> Exitings;
  // L->getExitingBlocks(Exitings);

  // Update PHINodes
  /// TODO: this work only if there is only one exiting block.
  ExitBlock->replacePhiUsesWith(Exiting, NewExitBlock);
  // Update terminator
  auto *Term = Exiting->getTerminator();
  Term->replaceSuccessorWith(ExitBlock, NewExitBlock);
  return NewExitBlock;
}

LoopMopInstrumenter::LoopMopInstrumenter(Function &F,
                                         FunctionAnalysisManager &FAM,
                                         LoopOptLeval OptLevel)
    : F(F), FAM(FAM), OptLevel(OptLevel) {
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
    relocateInvariantChecks();
    LoopChanged = combinePeriodicChecks(false);
    break;
  case LoopOptLeval::NoOpt:
    return;
  }

  if (LoopChanged) {
    // Update LoopInfo forcefully
    // auto &LI = FAM.getResult<LoopAnalysis>(F);
    FAM.invalidate(F, PreservedAnalyses::none());
  }
}

bool LoopMopInstrumenter::isSimpleLoop(const Loop *L) {
  if (!L)
    return false;
  if (SimpleLoops.contains(L))
    return true;
  if (ComplexLoops.contains(L))
    return false;

  /// TODO: relax this condition
  /// ONE latch, ONE predecessor, ONE header, ONE exiting block, ONE exit block
  if (!L->getHeader() || !L->getLoopPredecessor() || !L->getLoopLatch() ||
      !L->getExitBlock() || !L->getExitingBlock()) {
    ComplexLoops.insert(L);
    return false;
  }

  for (const BasicBlock *BB : L->getBlocks()) {
    for (const Instruction &I : *BB) {
      if (ShouldSkip(I))
        continue;
      if (!I.mayWriteToMemory())
        continue;
      /// TODO: should consider Atomic for ASan ?
      if (I.isAtomic() || isa<CallBase>(&I)) {
        ComplexLoops.insert(L);
        return false;
      }
    }
  }
  SimpleLoops.insert(L);
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
  auto &DT = FAM.getResult<DominatorTreeAnalysis>(F);
  auto &PDT = FAM.getResult<PostDominatorTreeAnalysis>(F);
  size_t count = 0;
  const auto &DL = F.getParent()->getDataLayout();

  SCEVExpander Expander(SE, DL, "expander");

  for (auto &BB : F) {
    for (auto &Inst : BB) {
      if (ShouldSkip(Inst))
        continue;
      Value *Addr = nullptr;
      size_t MopSize = 0;
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

      /// Only consider 8, 16, 32, 64, 128 bit access
      if (MopSize != 8 && MopSize != 16 && MopSize != 32 && MopSize != 64 &&
          MopSize != 128) {
        continue;
      }

      // bit size to byte size
      MopSize /= 8;

      auto *PtrSCEV = SE.getSCEV(Addr);
      if (!PtrSCEV)
        continue;

      /// TODO: transfer SCEVAddExpr(SCEVAddRecExpr) to
      /// SCEVAddRecExpr(SCEVAddExpr)
      const auto *AR = dyn_cast<SCEVAddRecExpr>(PtrSCEV);
      if (!AR)
        continue;

      const Loop *L = AR->getLoop();
      // We only handle those simple loops.
      if (!isSimpleLoop(L)) {
        continue;
      }

      const auto *Step = AR->getStepRecurrence(SE);

      // ----------- Extract Loop-Invariant step -----------
      const SCEVConstant *ConstStep = dyn_cast<SCEVConstant>(Step);
      if (!ConstStep) {
        // If not a constant, check if it is a Loop-Invariant
        if (!SE.isLoopInvariant(Step, L)) {
          continue; // Skip if not a loop invariant
        }
      }
      ConstantInt *StepVal = ConstStep->getValue();
      // Negative step is considered as positive step
      size_t StepValInt = StepVal->getSExtValue() < 0 ? -StepVal->getSExtValue()
                                                      : StepVal->getSExtValue();
      bool IsRangeAccess = (StepValInt == MopSize);
      if (RangeAccessOnly && !IsRangeAccess) {
        // If step is not MopSize, it's not full range access, skip
        continue;
      }

      auto *LoopLatch = L->getLoopLatch();
      if (!DT.dominates(Inst.getParent(), LoopLatch)) {
        // If Inst does not dominate the loop latch, it's within a loop branch,
        // skip analysis
        continue;
      }

      auto *ExitBlock = L->getUniqueExitBlock();
      if (!PDT.dominates(ExitBlock, LoopLatch)) {
        // If ExitBlock does not post-dominate the loop latch, it's within a
        // loop branch, skip analysis
        continue;
      }

      auto *Header = L->getHeader();
      auto *Exiting = L->getExitingBlock();
      if (!Exiting) {
        continue;
      }

      // If dominates Exiting, need to add 1 to the counter, otherwise no need
      // to add
      bool IsMopBeforeExiting = DT.dominates(Inst.getParent(), Exiting);

      // The only predecessor of exit should be the header.
      if (ExitBlock->getUniquePredecessor() != Header) {

        // Update ExitBlock
        ExitBlock = splitNewExitBlock(ExitBlock, Exiting);

        LoopChanged = true;
      }

      InstrumentationIRBuilder IRB(&*ExitBlock->getFirstInsertionPt());

      const auto *Start = AR->getStart();
      Value *Beg, *End;
      if (!expandBegAndEnd(const_cast<Loop *>(L), SE, Expander, Start,
                           ConstStep, IsMopBeforeExiting, IRB, Beg, End)) {
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

      MarkAsDelegatedToXsan(Inst);
      count++;
    }
  }

  if (!!getenv("XSAN_DEBUG") && count) {
    // print in orange color
    errs() << "\033[33m";
    errs() << "--- " << F.getName() << " ---\n";
    // print in grey color
    errs() << "\033[37m";
    errs() << "Combined Loop Mops: ";
    // print in red color
    errs() << "\033[31m";
    errs() << count;

    errs() << "\033[0m\n";
  }

  return LoopChanged;
}

void LoopMopInstrumenter::relocateInvariantChecks() {
  /// TODO:
}
} // namespace __xsan