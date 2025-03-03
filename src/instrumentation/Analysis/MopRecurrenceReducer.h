#pragma once

#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/PassManager.h"

#include "ActiveMopAnalysis.h"

namespace __xsan {
using namespace llvm;

/// Used for TSan's CompoundRW.
/// However, TSan does not support CompoundRW for now.
/// Therefore, we just return const* Instruction instead of MemoryOperation for
/// now.
enum class CheckMode { ReadOnly = 1, WriteOnly = 2, ReadWrite = 3 };

struct MemoryOperation {
  const MemoryLocation Loc;
  const Instruction *Inst;
  CheckMode Mode;

  MemoryOperation(const Instruction *I)
      : Loc(MemoryLocation::get(I)), Inst(I),
        Mode(I->mayWriteToMemory() ? CheckMode::WriteOnly
                                   : CheckMode::ReadOnly) {
    if (I->isVolatile()) {
      Mode = isa<LoadInst>(I) ? CheckMode::ReadWrite : CheckMode::WriteOnly;
    }
  }

  bool isWrite() const { return Mode == CheckMode::WriteOnly; }
  // Read + Write = ReadWrite.
  void mergeMode(CheckMode NewMode) {
    reinterpret_cast<uint8_t &>(Mode) |= static_cast<uint8_t>(NewMode);
  }
};

class MopRecurrenceReducer {
public:
  MopRecurrenceReducer(Function &F, FunctionAnalysisManager &FAM);

  /*
    MOP = (range, isWrite) defines a memory operation.
    For any TWO MOPs {MOP1 = (range1, isWrite1), MOP2 = (range2, isWrite2)} that
    satisfy the following conditions, the corresponding check for MOP2 is
    duplicated, abbreviated as ** MOP1 cover MOP2 **
      - Contains(range1, range2), considering aliases.
      - MOP1 dom MOP2 || MOP1 pdom MOP2
      - no-clobbering-calls between MOP1 and MOP2
      - ignore sanitizer-instrumented IRs and BBs.
      - (optional) isWrite1 || (isWrite1 == isWrite2)
    Notably, if MOP1 cover MOP2, and MOP2 cover MOP3, then MOP1 must cover MOP3.

    This duplication/redundancy define the edge relation between any two MOPs.
    We can construct a directed graph G = (V, E) using that relations.
    And then, the distilling of recurring checks can be transformed into a
    dominating set problem on directed graph G.

    In directed graph, the dominating set of vertices is the set of vertices
    that can reach any vertex in the graph.
    Any entry of SCC with ZERO in-degree belongs to the dominating set.
    -->   Any vertex with ZERO in-degree belongs to the dominating set.
        + Loop: 1. Add the vertex hasing not been traversed from any other
                   vertex into dominating set.
                2. Travese from any vertex in dominating set.
  */
  SmallVector<const Instruction *, 16>
  distillRecurringChecks(const SmallVectorImpl<const Instruction *> &Insts,
                         bool IsTsan, bool IgnoreCalls = false);

private:
  Function &F;
  FunctionAnalysisManager &FAM;
};

} // namespace __xsan
