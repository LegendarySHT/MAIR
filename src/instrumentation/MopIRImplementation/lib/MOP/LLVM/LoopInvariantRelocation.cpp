// 从原工程 Instrumentation.cpp / ValueUtils.cpp 迁移的「循环不变量检查外提」
// 实现，供 MopIR 独立编译；与原版共用同一 runtime 名与 xsan.delegate 元数据。

#include "MOP/LoopInvariantRelocationAdapter.h"

#ifdef MOP_IR_USE_LLVM

#include "MOP/MOPIRUnit.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

namespace {

unsigned getDelegateMDKind(LLVMContext &Ctx) {
  return Ctx.getMDKindID("xsan.delegate");
}

bool isDelegateMarked(const Instruction &I) {
  return I.getMetadata(getDelegateMDKind(I.getContext())) != nullptr;
}

void markDelegate(Instruction &I) {
  LLVMContext &Ctx = I.getContext();
  I.setMetadata(getDelegateMDKind(Ctx), MDNode::get(Ctx, None));
}

bool shouldSkipInstr(const Instruction &I) {
  return I.hasMetadata(LLVMContext::MD_nosanitize) || isDelegateMarked(I);
}

/// 与 Instrumentation.h 中 InstrumentationIRBuilder 一致：为生成的插桩打上 nosanitize。
class SanitizeIRBuilder : public IRBuilder<> {
  static void ensureNoSanitize(IRBuilder<> &IRB) {
    static ReturnInst *Tag = nullptr;
    if (!Tag) {
      Tag = ReturnInst::Create(IRB.getContext());
      Tag->setMetadata(LLVMContext::MD_nosanitize,
                       MDNode::get(Tag->getContext(), None));
    }
    IRB.CollectMetadataToCopy(Tag, {LLVMContext::MD_nosanitize});
  }

public:
  explicit SanitizeIRBuilder(Instruction *IP) : IRBuilder<>(IP) {
    ensureNoSanitize(*this);
  }
};

BlockAddress *getBlockAddressOfInstruction(Instruction &I, DominatorTree *DT,
                                           LoopInfo *LI,
                                           MemorySSAUpdater *MSSAU) {
  BasicBlock *OriginalBB = I.getParent();
  Instruction *FirstReal = OriginalBB->getFirstNonPHIOrDbgOrLifetime();
  if (FirstReal == &I)
    return BlockAddress::get(OriginalBB->getParent(), OriginalBB);
  BasicBlock *NewBB =
      SplitBlock(OriginalBB, &I, DT, LI, MSSAU, "mop.address", false);
  return BlockAddress::get(NewBB->getParent(), NewBB);
}

BasicBlock *splitKnownCriticalEdge(BasicBlock *From, BasicBlock *To,
                                   DominatorTree *DT, PostDominatorTree *PDT,
                                   LoopInfo *LI, MemorySSAUpdater *MSSAU,
                                   const Twine &BBName) {
  unsigned SuccNum = GetSuccessorNumber(From, To);
  Instruction *LatchTerm = From->getTerminator();
  CriticalEdgeSplittingOptions Options =
      CriticalEdgeSplittingOptions(DT, LI, MSSAU, PDT)
          .setMergeIdenticalEdges()
          .setPreserveLCSSA();
  BasicBlock *SplitBB =
      SplitKnownCriticalEdge(LatchTerm, SuccNum, Options, BBName);

  unsigned Identities = count(predecessors(SplitBB), From);
  for (PHINode &PN : SplitBB->phis()) {
    int Idx = PN.getBasicBlockIndex(From);
    assert(Idx >= 0 && "Invalid Block Index");
    Value *V = PN.getIncomingValue(Idx);
    unsigned CurIdentities = count(PN.blocks(), From);
    assert(CurIdentities <= Identities && "Invalid PHINode");
    for (unsigned i = 0; i < Identities - CurIdentities; ++i)
      PN.addIncoming(V, From);
  }
  return SplitBB;
}

Instruction *splitBlockAndInsertIfThen(Value *Cond, Instruction *SplitBefore,
                                       bool Unreachable, MDNode *BranchWeights,
                                       DominatorTree *DT, LoopInfo *LI,
                                       MemorySSAUpdater *MSSAU) {
  BasicBlock *OldBB = SplitBefore->getParent();
  Instruction *Term = llvm::SplitBlockAndInsertIfThen(
      Cond, SplitBefore, Unreachable, BranchWeights, DT, LI, nullptr);
  BasicBlock *NewBB = SplitBefore->getParent();
  if (MSSAU)
    MSSAU->moveAllAfterSpliceBlocks(OldBB, NewBB, SplitBefore);
  return Term;
}

bool shouldInstrumentReadWriteFromAddress(const Module *M, Value *Addr) {
  Addr = Addr->stripInBoundsOffsets();
  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(Addr)) {
    if (GV->hasSection()) {
      StringRef SectionName = GV->getSection();
      auto OF = Triple(M->getTargetTriple()).getObjectFormat();
      if (SectionName.endswith(
              getInstrProfSectionName(IPSK_cnts, OF, /*AddSegmentInfo=*/false)))
        return false;
    }
    if (GV->getName().startswith("__llvm_gcov") ||
        GV->getName().startswith("__llvm_gcda"))
      return false;
  }
  if (Addr) {
    Type *PtrTy = cast<PointerType>(Addr->getType()->getScalarType());
    if (PtrTy->getPointerAddressSpace() != 0)
      return false;
  }
  return true;
}

class LoopInvariantChecker {
public:
  LoopInvariantChecker(const DominatorTree &DT, AAResults &AA)
      : DT(DT), AA(AA) {
    const Module &M = *DT.getRoot()->getModule();
    UBSanExists = any_of(M.getFunctionList(), [&](const Function &F) {
      return F.isDeclaration() && F.getName().startswith("__ubsan_handle");
    });
  }

  bool isLoopInvariant(Value *V, const Loop *L) {
    bool NotInLoop = L->isLoopInvariant(V);
    if (!UBSanExists || NotInLoop)
      return NotInLoop;
    auto *I = dyn_cast<Instruction>(V);
    if (!I)
      return true;
    if (isa<PHINode>(I))
      return false;
    if (!L->hasLoopInvariantOperands(I))
      return false;
    if (LoadInst *Load = dyn_cast<LoadInst>(I))
      return isLoadLoopInvariant(Load, L);
    return true;
  }

  bool isLoopInvariant(ScalarEvolution &SE, const SCEV *S, const Loop *L) {
    return UBSanExists ? isLoopInvariantSCEV(S, L) : SE.isLoopInvariant(S, L);
  }

private:
  bool isLoopInvariantSCEV(const SCEV *S, const Loop *L) {
    switch (S->getSCEVType()) {
    case scConstant:
      return true;
    case scPtrToInt:
    case scTruncate:
    case scZeroExtend:
    case scSignExtend:
      return isLoopInvariantSCEV(cast<SCEVCastExpr>(S)->getOperand(), L);
    case scAddRecExpr: {
      const SCEVAddRecExpr *AR = cast<SCEVAddRecExpr>(S);
      if (AR->getLoop() == L)
        return false;
      if (!L)
        return false;
      if (DT.dominates(L->getHeader(), AR->getLoop()->getHeader()))
        return false;
      assert(!L->contains(AR->getLoop()) &&
             "Containing loop's header does not dominate the contained loop's "
             "header?");
      if (AR->getLoop()->contains(L))
        return true;
      for (const auto *Op : AR->operands())
        if (!isLoopInvariantSCEV(Op, L))
          return false;
      return true;
    }
    case scAddExpr:
    case scMulExpr:
    case scUMaxExpr:
    case scSMaxExpr:
    case scUMinExpr:
    case scSMinExpr:
    case scSequentialUMinExpr: {
      for (const auto *Op : cast<SCEVNAryExpr>(S)->operands()) {
        if (!isLoopInvariantSCEV(Op, L))
          return false;
      }
      return true;
    }
    case scUDivExpr: {
      const SCEVUDivExpr *UDiv = cast<SCEVUDivExpr>(S);
      return isLoopInvariantSCEV(UDiv->getLHS(), L) &&
             isLoopInvariantSCEV(UDiv->getRHS(), L);
    }
    case scUnknown:
      return isLoopInvariant(cast<SCEVUnknown>(S)->getValue(), L);
    case scCouldNotCompute:
      llvm_unreachable("Attempt to use a SCEVCouldNotCompute object!");
    }
    llvm_unreachable("Unknown SCEV kind!");
  }

  bool isLoadLoopInvariant(const LoadInst *LI, const Loop *L) {
    if (LI->isVolatile() || LI->isAtomic())
      return false;
    if (!L->hasLoopInvariantOperands(LI))
      return false;
    SmallVectorImpl<MemoryLocation> &StoreLocs = getStoresInLoopLazily(L);
    if (StoreLocs.empty())
      return true;
    MemoryLocation LoadLoc = MemoryLocation::get(LI);
    return all_of(StoreLocs, [&](auto &StoreLoc) {
      return AA.alias(LoadLoc, StoreLoc) == AliasResult::NoAlias;
    });
  }

  SmallVectorImpl<MemoryLocation> &getStoresInLoopLazily(const Loop *L) {
    auto It = StoresInLoop.find(L);
    if (It != StoresInLoop.end())
      return It->second;
    SmallVector<MemoryLocation, 8> Stores;
    for (BasicBlock *BB : L->getBlocks()) {
      for (Instruction &I : *BB) {
        if (!I.mayWriteToMemory())
          continue;
        if (StoreInst *SI = dyn_cast<StoreInst>(&I))
          Stores.push_back(MemoryLocation::get(SI));
      }
    }
    StoresInLoop[L] = std::move(Stores);
    return StoresInLoop[L];
  }

  bool UBSanExists = false;
  const DominatorTree &DT;
  AAResults &AA;
  DenseMap<const Loop *, SmallVector<MemoryLocation, 8>> StoresInLoop;
};

struct LoopMop {
  Instruction *Mop = nullptr;
  Value *Address = nullptr;
  Loop *L = nullptr;
  size_t MopSize = 0;
  SmallVector<Instruction *, 4> DupTo;
  bool InBranch = false;
  bool IsWrite = false;
};

class LoopInvariantRelocationEngine {
  static constexpr size_t kNumberOfAccessSizes = 5;

public:
  LoopInvariantRelocationEngine(Function &Func, FunctionAnalysisManager &Fam)
      : F(Func), FAM(Fam), LI(FAM.getResult<LoopAnalysis>(F)),
        MSSAU(&FAM.getResult<MemorySSAAnalysis>(F).getMSSA()),
        AA(FAM.getResult<AAManager>(F)), DT(FAM.getResult<DominatorTreeAnalysis>(F)),
        PDT(FAM.getResult<PostDominatorTreeAnalysis>(F)),
        DL(F.getParent()->getDataLayout()), MopCollected(false), LIC(DT, AA) {
    Module &M = *F.getParent();
    LLVMContext &Ctx = M.getContext();
    IRBuilder<> IRB(Ctx);
    AttributeList Attr =
        AttributeList::get(Ctx, AttributeList::FunctionIndex, Attribute::NoUnwind);
    for (size_t i = 0; i < kNumberOfAccessSizes; ++i) {
      const unsigned ByteSize = 1U << i;
      std::string ByteSizeStr = utostr(ByteSize);
      XsanRead[i] = M.getOrInsertFunction(
          "__xsan_read" + ByteSizeStr, Attr, IRB.getVoidTy(), IRB.getInt8PtrTy(),
          IRB.getInt64Ty());
      XsanWrite[i] = M.getOrInsertFunction(
          "__xsan_write" + ByteSizeStr, Attr, IRB.getVoidTy(), IRB.getInt8PtrTy(),
          IRB.getInt64Ty());
    }
  }

  void run(MopIRImpl::MOP::MOPIRUnit *HighLevelUnit = nullptr) {
    if (F.isDeclaration() || F.empty())
      return;
    if (HighLevelUnit) {
      for (auto &Up : HighLevelUnit->getMops()) {
        if (Up)
          Up->setLoopHoistPlanned(false);
      }
      annotateHighLevelMops(*HighLevelUnit);
    }
    (void)relocateInvariantChecks();
    PreservedAnalyses PA = PreservedAnalyses::all();
    PA.abandon<PostDominatorTreeAnalysis>();
    FAM.invalidate(F, PA);
  }

private:
  /// 在 relocateInvariantChecks 改写 IR 之前，对与即将外提的 load/store 对应的
  /// 高阶 MOP 置位，便于论文/调试观察高阶层上的循环优化决策。
  void annotateHighLevelMops(MopIRImpl::MOP::MOPIRUnit &U) {
    llvm::DenseMap<llvm::Instruction *, MopIRImpl::MOP::Mop *> InstToMop;
    for (auto &Up : U.getMops()) {
      if (!Up)
        continue;
      if (llvm::Instruction *Insn =
              const_cast<llvm::Instruction *>(Up->getInstruction()))
        InstToMop[Insn] = Up.get();
    }

    Instruction *LastInsertPt = nullptr;
    BasicBlock *LastBB = nullptr;
    for (LoopMop &LM : getLoopMopCandidates()) {
      auto &[Inst, Addr, L, MopSize, DupTo, InBranch, IsWrite] = LM;
      if (!LIC.isLoopInvariant(Addr, L))
        continue;
      Loop *TopL = L, *ParentL = L->getParentLoop();
      while (ParentL && LIC.isLoopInvariant(Addr, ParentL) &&
             isSimpleLoop(ParentL)) {
        TopL = ParentL;
        ParentL = TopL->getParentLoop();
      }
      BasicBlock *Preheader = TopL->getLoopPreheader();
      bool IsInBranch =
          InBranch ? InBranch
                   : (!Preheader ||
                      !PDT.dominates(Inst->getParent(), Preheader));
      Instruction *InsertPt = nullptr;
      bool SameBBWithLast = LastBB && LastBB == Inst->getParent();
      bool HoistToPreheader = !IsInBranch && Preheader;
      if (SameBBWithLast) {
        InsertPt = LastInsertPt;
      } else if (HoistToPreheader) {
        InsertPt = Preheader->getTerminator();
      } else {
        // 外提引擎在真正改写时可能分裂关键边或插入指示变量；此处不修改
        // LLVM IR，故仅对「无需上述 IR 手术即可确定插入点」的情形标注 MOP。
        BasicBlock *ExitBlock = TopL->getUniqueExitBlock();
        BasicBlock *Exiting = TopL->getExitingBlock();
        if (ExitBlock->getUniquePredecessor() != Exiting)
          continue;
        if (IsInBranch)
          continue;
        InsertPt = &*ExitBlock->getFirstInsertionPt();
      }
      (void)MopSize;
      (void)IsWrite;
      (void)DupTo;
      if (MopIRImpl::MOP::Mop *Hi = InstToMop.lookup(Inst))
        Hi->setLoopHoistPlanned(true);
      LastInsertPt = InsertPt;
      LastBB = Inst->getParent();
    }
  }

  SmallVectorImpl<LoopMop> &getLoopMopCandidates() {
    if (!MopCollected)
      collectLoopMopCandidates();
    return LoopMopCandidates;
  }

  void collectLoopMopCandidates() {
    SmallVector<LoopMop, 16> LoopMops;
    for (BasicBlock &BB : F) {
      Loop *Loop = LI.getLoopFor(&BB);
      if (!Loop || !isSimpleLoop(Loop))
        continue;
      for (Instruction &Inst : BB) {
        if (shouldSkipInstr(Inst))
          continue;
        if (Inst.isVolatile() || Inst.isAtomic() || isa<DbgInfoIntrinsic>(Inst))
          continue;
        Value *Addr = nullptr;
        size_t MopSize = 0;
        bool InBranch = false;
        bool IsWrite = false;
        if (auto *SI = dyn_cast<StoreInst>(&Inst)) {
          Addr = SI->getPointerOperand();
          MopSize = DL.getTypeStoreSizeInBits(SI->getValueOperand()->getType());
          IsWrite = true;
        } else if (auto *Ld = dyn_cast<LoadInst>(&Inst)) {
          Addr = Ld->getPointerOperand();
          MopSize = DL.getTypeStoreSizeInBits(Ld->getType());
          IsWrite = false;
        } else if (isa<CallBase>(Inst) && Inst.mayWriteToMemory()) {
          filterAndAddMops(LoopMops);
          LoopMops.clear();
          continue;
        }
        if (!Addr || !shouldInstrumentReadWriteFromAddress(Inst.getModule(), Addr))
          continue;
        if (MopSize != 8 && MopSize != 16 && MopSize != 32 && MopSize != 64 &&
            MopSize != 128)
          continue;
        MopSize /= 8;
        BasicBlock *LoopLatch = Loop->getLoopLatch();
        assert(LoopLatch && "Loop must have single latch");
        if (!DT.dominates(Inst.getParent(), LoopLatch))
          InBranch = true;
        LoopMops.push_back({&Inst, Addr, Loop, MopSize, {}, InBranch, IsWrite});
      }
      filterAndAddMops(LoopMops);
      LoopMops.clear();
    }
    MopCollected = true;
  }

  void filterAndAddMops(SmallVectorImpl<LoopMop> &MOPs) {
    if (MOPs.empty())
      return;
    if (MOPs.size() == 1) {
      LoopMopCandidates.push_back(std::move(MOPs.pop_back_val()));
      return;
    }
    SmallMapVector<Value *, SmallVector<Instruction *, 4> *, 4> SeenAddrs;
    SmallVector<LoopMop, 8> MopsReversed;
    MopsReversed.reserve(MOPs.size());
    for (LoopMop &Mop : reverse(MOPs)) {
      auto &[Inst, Addr, Loop, MopSize, DupTo, InBranch, IsWrite] = Mop;
      auto *It = SeenAddrs.find(Addr);
      if (It != SeenAddrs.end()) {
        It->second->push_back(Inst);
      } else {
        MopsReversed.push_back(std::move(Mop));
        SeenAddrs[Addr] = &MopsReversed.back().DupTo;
      }
    }
    LoopMopCandidates.append(std::make_move_iterator(MopsReversed.rbegin()),
                             std::make_move_iterator(MopsReversed.rend()));
  }

  bool isSimpleLoop(const Loop *Loop) {
    if (!Loop)
      return false;
    if (SimpleLoops.contains(Loop))
      return true;
    if (ComplexLoops.contains(Loop))
      return false;
    if (!Loop->getHeader() || !Loop->getLoopPredecessor() ||
        !Loop->getLoopLatch() || !Loop->getUniqueExitBlock() ||
        !Loop->getExitingBlock()) {
      ComplexLoops.insert(Loop);
      return false;
    }
    for (const BasicBlock *BB : Loop->getBlocks()) {
      for (const Instruction &I : *BB) {
        if (shouldSkipInstr(I))
          continue;
        if (!I.mayWriteToMemory())
          continue;
        if (isa<DbgInfoIntrinsic>(I))
          continue;
        if (I.isAtomic() || isa<CallBase>(&I)) {
          ComplexLoops.insert(Loop);
          return false;
        }
      }
    }
    SimpleLoops.insert(Loop);
    return true;
  }

  Instruction *instrumentIndicator(Instruction *Inst, BasicBlock *ExitBlock) {
    Function *Fn = Inst->getParent()->getParent();
    BasicBlock &Entry = Fn->getEntryBlock();
    SanitizeIRBuilder IRB(&*Entry.getFirstInsertionPt());
    auto *IndicatorAlloc =
        IRB.CreateAlloca(IRB.getInt1Ty(), nullptr, "indicator");
    IRB.CreateStore(IRB.getFalse(), IndicatorAlloc);
    IRB.SetInsertPoint(Inst);
    IRB.CreateStore(IRB.getTrue(), IndicatorAlloc);
    IRB.SetInsertPoint(&*ExitBlock->getFirstInsertionPt());
    auto *Indicator = IRB.CreateLoad(IRB.getInt1Ty(), IndicatorAlloc);
    Instruction *EqTrue =
        cast<Instruction>(IRB.CreateICmpEQ(Indicator, IRB.getTrue()));
    return splitBlockAndInsertIfThen(EqTrue, EqTrue->getNextNode(), false,
                                     nullptr, &DT, &LI, &MSSAU);
  }

  unsigned tagMopAsDelegated(LoopMop &Mop) {
    markDelegate(*Mop.Mop);
    for (Instruction *DupInst : Mop.DupTo)
      markDelegate(*DupInst);
    return Mop.DupTo.size();
  }

  bool relocateInvariantChecks() {
    bool LoopChanged = false;
    Instruction *LastInsertPt = nullptr;
    BasicBlock *LastBB = nullptr;
    for (LoopMop &Mop : getLoopMopCandidates()) {
      auto &[Inst, Addr, L, MopSize, DupTo, InBranch, IsWrite] = Mop;
      if (!LIC.isLoopInvariant(Addr, L))
        continue;
      Loop *TopL = L, *ParentL = L->getParentLoop();
      while (ParentL && LIC.isLoopInvariant(Addr, ParentL) &&
             isSimpleLoop(ParentL)) {
        TopL = ParentL;
        ParentL = TopL->getParentLoop();
      }
      BasicBlock *Preheader = TopL->getLoopPreheader();
      bool IsInBranch =
          InBranch ? InBranch
                   : (!Preheader ||
                      !PDT.dominates(Inst->getParent(), Preheader));
      Instruction *InsertPt = nullptr;
      bool SameBBWithLast = LastBB && LastBB == Inst->getParent();
      auto *AddrInst = dyn_cast<Instruction>(Addr);
      bool HoistToPreheader = !IsInBranch && Preheader;
      if (SameBBWithLast) {
        InsertPt = LastInsertPt;
      } else if (HoistToPreheader) {
        InsertPt = Preheader->getTerminator();
      } else {
        BasicBlock *ExitBlock = TopL->getUniqueExitBlock();
        BasicBlock *Exiting = TopL->getExitingBlock();
        if (ExitBlock->getUniquePredecessor() != Exiting) {
          ExitBlock = splitKnownCriticalEdge(Exiting, ExitBlock, &DT, nullptr,
                                               &LI, &MSSAU, "xsan.loop.exit");
          LoopChanged = true;
        }
        if (IsInBranch) {
          InsertPt = instrumentIndicator(Inst, ExitBlock);
          if (!InsertPt)
            continue;
        } else {
          InsertPt = &*ExitBlock->getFirstInsertionPt();
        }
      }
      if (AddrInst && !DT.dominates(AddrInst, InsertPt)) {
        Instruction *ClonedAddr = AddrInst->clone();
        ClonedAddr->insertBefore(InsertPt);
        ClonedAddr->setMetadata(LLVMContext::MD_nosanitize,
                                MDNode::get(AddrInst->getContext(), None));
        Addr = ClonedAddr;
      }
      size_t Idx = countTrailingZeros(MopSize);
      SanitizeIRBuilder IRB(InsertPt);
      Constant *BlockAddr =
          getBlockAddressOfInstruction(*Mop.Mop, &DT, &LI, &MSSAU);
      Value *PcValue = IRB.CreatePtrToInt(BlockAddr, IRB.getInt64Ty());
      IRB.CreateCall(IsWrite ? XsanWrite[Idx] : XsanRead[Idx], {Addr, PcValue});
      (void)tagMopAsDelegated(Mop);
      LastInsertPt = InsertPt;
      LastBB = Inst->getParent();
    }
    (void)LoopChanged;
    return LoopChanged;
  }

  Function &F;
  FunctionAnalysisManager &FAM;
  LoopInfo &LI;
  MemorySSAUpdater MSSAU;
  AAResults &AA;
  DominatorTree &DT;
  PostDominatorTree &PDT;
  const DataLayout &DL;
  SmallPtrSet<const Loop *, 16> SimpleLoops;
  SmallPtrSet<const Loop *, 16> ComplexLoops;
  SmallVector<LoopMop, 16> LoopMopCandidates;
  bool MopCollected;
  FunctionCallee XsanRead[kNumberOfAccessSizes];
  FunctionCallee XsanWrite[kNumberOfAccessSizes];
  LoopInvariantChecker LIC;
};

} // namespace

namespace MopIRImpl {
namespace MOP {

void runLoopInvariantRelocationOnly(Function &F, FunctionAnalysisManager &FAM) {
  runLoopInvariantRelocationOnly(F, FAM, nullptr);
}

void runLoopInvariantRelocationOnly(Function &F, FunctionAnalysisManager &FAM,
                                    MOPIRUnit *HighLevelUnit) {
  LoopInvariantRelocationEngine Eng(F, FAM);
  Eng.run(HighLevelUnit);
}

} // namespace MOP
} // namespace MopIRImpl

#endif // MOP_IR_USE_LLVM
