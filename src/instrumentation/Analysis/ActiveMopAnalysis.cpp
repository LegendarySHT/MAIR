//===- ActiveMopAnalysis.cpp - Active Memory Operations Analysis -*-C++-*--===//
//
// This file implements the ReachableMOPs class, which performs a static
// dataflow analysis to determine reachable Memory Operations (MOPs) within an
// LLVM Function. The analysis uses a worklist algorithm and leverages LLVM's
// data structures for efficiency.
//
//===----------------------------------------------------------------------===//

#include "ActiveMopAnalysis.h"

#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h" // For llvm::reverse
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>

using namespace llvm;

namespace __xsan {

/// @brief Checks if an instruction clobbers memory.
/// - no-side-effected-calls between MOP1 and MOP2
static bool isClobberingCall(const Instruction *Inst,
                             bool IgnoreNoSanitized = true) {
  // Check call/invoke instruction
  if (!isa<CallBase>(Inst)) {
    return false;
  }

  if (isa<DbgInfoIntrinsic>(Inst)) {
    return false;
  }
  // Check call decorated with non-sanitize metadata
  /// TODO: use other metadata? nosanitize metadata might be used by users.
  if (IgnoreNoSanitized && Inst->hasMetadata(LLVMContext::MD_nosanitize))
    return false;

  if (!Inst->mayWriteToMemory())
    return false;

  return true;
}

/// @brief Checks if one instruction is reachable from another within the same
/// basic block.
static bool isActiveInTheSameBlock(const Instruction *From,
                                   const Instruction *To) {
  const Instruction *Inst = From;
  do {
    if (Inst == To) {
      return true;
    }
    if (isClobberingCall(Inst)) {
      return false;
    }
  } while ((Inst = Inst->getNextNonDebugInstruction()));

  llvm_unreachable("From and To should be in the same basic block and FromI "
                   "should be before ToI");
}

static bool isReachableAmongBlocks(const Instruction *From,
                                   const Instruction *To) {
  const BasicBlock *FromBB = From->getParent();
  const BasicBlock *ToBB = To->getParent();
  if (FromBB == ToBB) {
    return isActiveInTheSameBlock(From, To);
  }

  if (!isActiveInTheSameBlock(ToBB->getFirstNonPHIOrDbgOrLifetime(), To) ||
      !isActiveInTheSameBlock(From, FromBB->getTerminator())) {
    return false;
  }

  for (const auto *BB : depth_first(FromBB)) {
    if (BB == ToBB) {
    }
  }

  llvm_unreachable("From and To should be in the same basic block and From "
                   "should be before To");
}

ActiveMopAnalysis::Lattice::Lattice(unsigned NumMops)
    : Reachable(NumMops, false), NotActive(NumMops, false) {}

void ActiveMopAnalysis::Lattice::kill() {
  /// ⊥ → ⊥, else -> ⊤
  /// Can be implemented as NotActive = Reachable.
  NotActive = Reachable;
}

void ActiveMopAnalysis::Lattice::join(const Lattice &Other) {
  Reachable |= Other.Reachable;
  NotActive |= Other.NotActive;
}

bool ActiveMopAnalysis::Lattice::operator==(const Lattice &Other) const {
  return Reachable == Other.Reachable && NotActive == Other.NotActive;
}

SmallVector<const Instruction *, 64>
ActiveMopAnalysis::extractMOPs(const Function &F) const {
  SmallVector<const Instruction *, 64> MOPs;
  for (const BasicBlock &BB : F) {
    for (const Instruction &I : BB) {
      if (isInterestingMop(I)) {
        MOPs.push_back(&I);
      }
    }
  }
  return MOPs;
}

void ActiveMopAnalysis::initializeMopMaps() {
  MopMap.clear();

  unsigned Id = 0;

  for (auto *I : MopList) {
    MopMap[I] = Id;
    Id++;
  }
}

ActiveMopAnalysis::MopID
ActiveMopAnalysis::getMopId(const Instruction *I) const {
  auto it = MopMap.find(I);
  assert(it != MopMap.end() && "Instruction does not exist in MopMap");
  return it->second;
}

void ActiveMopAnalysis::initializeBlockInfo(const Function &F) {
  unsigned NumMoPs = MopList.size();

  for (const BasicBlock &BB : F) {
    BlockInfo Val = getInitializedBlockInfo(BB);
    BlockInfoMap.try_emplace(&BB, std::move(Val));
  }
}

ActiveMopAnalysis::BlockInfo
ActiveMopAnalysis::getInitializedBlockInfo(const BasicBlock &BB) const {
  unsigned NumMops = MopList.size();
  BitVectorSet GenSet(NumMops, false);
  BitVectorSet UseGen(NumMops, false);
  BitVectorSet NotUseGen(NumMops, false);

  bool ContainsCall = false;

  // Traverse instructions in reverse to find MOPs that are not followed by
  // calls.
  for (const Instruction &I : reverse(BB.getInstList())) {
    if (!ContainsCall && isClobberingCall(&I)) {
      ContainsCall = true;
    }
    if (isInterestingMop(I)) {
      auto MapIt = MopMap.find(&I);
      if (MapIt != MopMap.end()) {
        MopID MopId = MapIt->second;
        UseGen.set(MopId);
        NotUseGen.set(MopId);
        if (ContainsCall) {
          GenSet.set(MopId);
        }
      }
    }
  }

  NotUseGen.flip();

  return {Lattice(NumMops),     Lattice(NumMops),  std::move(UseGen),
          std::move(NotUseGen), std::move(GenSet), ContainsCall};
}

const ActiveMopAnalysis::BlockInfo &
ActiveMopAnalysis::getBlockInfo(const BasicBlock *BB) const {
  auto It = BlockInfoMap.find(BB);
  assert(It != BlockInfoMap.end() &&
         "BasicBlock does not exist in BlockInfoMap");
  return It->getSecond();
}

ActiveMopAnalysis::BlockInfo &
ActiveMopAnalysis::getBlockInfo(const BasicBlock *BB) {
  auto It = BlockInfoMap.find(BB);
  assert(It != BlockInfoMap.end() &&
         "BasicBlock does not exist in BlockInfoMap");
  return It->getSecond();
}

bool ActiveMopAnalysis::isMopActiveAfter(MopID Mop,
                                         const BasicBlock *BB) const {
  const BlockInfo &Info = getBlockInfo(BB);
  return Info.OUT.isActive(Mop);
}

bool ActiveMopAnalysis::isMopActiveBefore(MopID Mop,
                                          const BasicBlock *BB) const {
  const BlockInfo &Info = getBlockInfo(BB);
  return Info.IN.isActive(Mop);
}

void ActiveMopAnalysis::printInfo(const BlockInfo &Info,
                                  raw_ostream &OS) const {
  OS << "  IN: \n";
  for (MopID id = 0; id < MopList.size(); ++id) {
    if (Info.IN.isActive(id)) {
      OS << "\t" << *MopList[id] << "\n";
    }
  }
  OS << "\n";

  OS << "  OUT: \n";
  for (MopID id = 0; id < MopList.size(); ++id) {
    if (Info.OUT.isActive(id)) {
      OS << "\t" << *MopList[id] << "\n";
    }
  }
  OS << "------\n";
}

bool ActiveMopAnalysis::suitableToAnlyze() {
  if (!MopList.size()) {
    return false;
  }

  bool CrossBlocks = false;
  const BasicBlock *BB = MopList[0]->getParent();
  for (const auto *I : MopList) {
    if (I->getParent() != BB) {
      CrossBlocks = true;
      break;
    }
  }
  return CrossBlocks;
}

ActiveMopAnalysis::ActiveMopAnalysis(
    Function &F, const SmallVectorImpl<const Instruction *> &MOPs)
    : MopList(MOPs.begin(), MOPs.end()), F(F), Analyzed(false) {
  if (suitableToAnlyze())
    dataflowAnalyze();
}

ActiveMopAnalysis::ActiveMopAnalysis(Function &F)
    : ActiveMopAnalysis(F, extractMOPs(F)) {}

bool ActiveMopAnalysis::updateInfo(const BasicBlock *BB) {
  BlockInfo &Info = getBlockInfo(BB);

  // Merge
  /// IN[B] = ∪_{P ∈ pre(B)} OUT[P]
  for (const BasicBlock *Pred : predecessors(BB)) {
    const BlockInfo &PredInfo = getBlockInfo(Pred);
    Info.IN.join(PredInfo.OUT);
  }

  // Transfer
  /// OUT = UseGen? Gen : (HasCall? Kill(IN) : IN)
  Lattice NewOUT = transfer(Info);

  if (NewOUT == Info.OUT) {
    return false;
  }

  Info.OUT = std::move(NewOUT);
  return true;
}

ActiveMopAnalysis::Lattice
ActiveMopAnalysis::transfer(const BlockInfo &Info) const {
  /// Implace version of tranfer function.
  Lattice NewOUT = Info.IN;
  // OUT = (HasCall? Kill(IN) : IN)
  // => OUT = IN
  //    if (UseKill) OUT.kill()
  if (Info.ContainsCall) {
    NewOUT.kill();
  }

  // OUT = UseGen? Gen : (HasCall? Kill(IN) : IN)
  // ==>
  //       Gen = {~, ⊤} --> {10, 11}
  //    UseGen = 1 -->  OUT.reach = 1
  NewOUT.Reachable |= Info.UseGen;
  // OUT = UseGen? Gen : (HasCall? Kill(IN) : IN)
  // ==> OUT = UseGen? Gen : OUT
  // ==> OUT = (UseGen & Gen) | (~UseGen & OUT)
  // ==> OUT &= ~UseGen
  //     OUT |= (UseGen & Gen) # Gen = UseGen & Gen naturally
  NewOUT.NotActive &= Info.NotUseGen;
  NewOUT.NotActive |= Info.Gen;

  return NewOUT;
}

/// TODO: SCC and DAG
void ActiveMopAnalysis::dataflowAnalyze() {
  if (F.isDeclaration() || F.empty())
    return;

  initializeMopMaps();
  initializeBlockInfo(F);

  // Initialize the worklist using SmallSetVector to avoid duplicates.
  SmallSetVector<const BasicBlock *, 64> WorkList;

  // Use reverse post-order traversal first to speed up the iteration.
  ReversePostOrderTraversal<const Function *> RPOT(&F);
  for (const auto *BB : RPOT) {
    // Erase BB from worklist as we will update its info in the next line.
    WorkList.remove(BB);

    // Update the block info for BB.
    updateInfo(BB);

    // Propagate to successors.
    // As we erase BB in the beginning, the enrties of
    // loops will retain after the ReverserPostOrderTraversal ends.
    for (const BasicBlock *Succ : successors(BB)) {
      WorkList.insert(Succ);
    }
  }

  while (!WorkList.empty()) {
    const BasicBlock *BB = WorkList.pop_back_val();

    if (!updateInfo(BB)) {
      continue;
    }

    // Propagate changes to successors.
    for (const BasicBlock *Succ : successors(BB)) {
      WorkList.insert(Succ);
    }
  }

  Analyzed = true;
}

bool ActiveMopAnalysis::isOneMopActiveToAnother(const Instruction *From,
                                                const Instruction *To) const {
  const BasicBlock *FromBB = From->getParent();
  const BasicBlock *ToBB = To->getParent();

  if (!Analyzed) {
    assert(FromBB == ToBB && "From and To should be in the same basic block");
    return isActiveInTheSameBlock(From, To);
  }

  MopID FromId = getMopId(From);

  // If a MOP is reachable in the OUT[ToBB], then it must be reachable for To.
  if (isMopActiveAfter(FromId, ToBB)) {
    return true;
  }

  // If From and To belong to the same basic block, then we can use the
  // internal reachability test.
  if (FromBB == ToBB) {
    return isActiveInTheSameBlock(From, To);
  }

  // If the external From is not reachable in the IN[ToBB], then it must not be
  // reachable for To.
  if (!isMopActiveBefore(FromId, ToBB)) {
    assert(ToBB != FromBB && "ToBB should not be the same as FromBB");
    return false;
  }

  // Eventually, we need to check if the entry of the ToBB can reach To.
  const Instruction *Entry = ToBB->getFirstNonPHIOrDbgOrLifetime();
  return isActiveInTheSameBlock(Entry, To);
}

const SmallVector<const Instruction *, 64> &
ActiveMopAnalysis::getMOPList() const {
  return MopList;
}

void ActiveMopAnalysis::printAnalysis(raw_ostream &OS) {
  if (F.empty() || F.isDeclaration()) {
    OS << "Function is empty or is a declaration\n";
    return;
  }

  if (!Analyzed) {
    dataflowAnalyze();
  }

  // Print MopList
  OS << "------------------------\n";
  OS << "MopList:\n";
  for (const Instruction *I : MopList) {
    OS << "\t" << *I << "\n";
  }
  OS << "------------------------\n";

  for (const auto &[BB, Info] : BlockInfoMap) {
    OS << "BasicBlock: ";
    BB->printAsOperand(OS);
    OS << "\n";

    printInfo(Info, OS);
    OS << "\n";
  }
}

} // namespace __xsan
