#pragma once

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

namespace __xsan {

using namespace llvm;

// - ignore sanitizer-instrumented IRs and BBs.
static bool isInterestingMop(const Instruction &I, bool IgnoreAtomic = true) {
  if (I.hasMetadata(LLVMContext::MD_nosanitize))
    return false;
  if (IgnoreAtomic && I.isAtomic())
    return false;
  return isa<StoreInst>(I) || isa<LoadInst>(I);
}

/*
 * Analysis Overview: Active Memory Operation Propagation, i.e.,
 *
 * **Objective:**
 *      Determine whether a memory operation (MOP1) isreachable to
 *      another memory operation (MOP2) without any calls in between.
 *
 * Feasibility Analysis:
 *   - The problem can be transformed into a query at specific program points.
 *   - Analysis can be conducted directly on the instruction graph.
 *   - When utilizing the Control Flow Graph (CFG), the problem can be
 *     decomposed into a two-step query:
 *       1. **Inter-block Analysis:** Determine whether MOP1 is live at the
 *          entry of the basic block containing MOP2.
 *       2. **Intra-block Analysis:** Assess whether the entry of the basic
 *          block containing MOP2 renders MOP1 live at MOP2.
 *
 * Graph Structure:
 *   - The Control Flow Graph (CFG) is selected as the primary graph structure
 *     for the analysis.
 *   - Utilizing CFG simplifies the analysis, negating the necessity for more
 *     complex graph representations.
 *
 * Object Set:
 *   - The analysis focuses on all memory access operations (MOPs) within each
 *     function.
 *
 * Then, we can define the THREE components (D, L, F) of the dataflow analysis:
 *
 * D: Analysis Direction:
 *   - Forward analysis is employed because function calls within basic blocks
 *     invalidate the liveness of MOPs at the block's exit.
 *
 * L: Lattice Design:
 *   - A three-point lattice is defined as {⊥, ~, ⊤}, where:
 *       - ⊥ represents "Not Reachable."
 *       - ~ represents "Reachable and Live."
 *       - ⊤ represents "Reachable but Not Live."
 *   - In fact, we use two {0,1}^m to represent the domain L^m, for analytical
 *     efficiency reasons.
 *   - An alternative two-point lattice {0, 1} was considered but discarded due
 *     to the loss of reachability information, which compromises the accuracy
 *     of the static analysis.
 *
 * F: Function Family Design:
 *   1. **Merge Function:**
 *       - The merge function is designed to reflect a conservative approach,
 *         ensuring that analysis results remain safe.
 *       - Since "Reachable but Not Live" is a safe state, the join operation
 *         (logical OR, ∨) is utilized to compute the least upper bound.
 *       - The initial state is set to ⊥.
 *
 *   2. **Transfer Function (f_b^(m)) on I × M:**
 *       - Consider a basic block BB with:
 *           - The last function call denoted as c.
 *           - The terminator instruction denoted as t.
 *           - An instruction sequence within BB represented as [a, b].
 *
 *       - **Kill Function (kill_b^(m)):**
 *           kill_b^(m) =
 *               ⊥ → ⊥
 *               ~, ⊤ → ⊤
 *
 *       - **Transfer Function Definition:**
 *           f_b^(m)(i) =
 *               if (NoCalls(BB) and m ∉ BB):
 *                   return i
 *               elif (m ∈ BB):
 *                   return m ∈ [c, t] ? ~ : ⊤
 *               else:
 *                   return kill_b^(m)(i)
 *           i.e., OUT = UseGen? Gen : (HasCall? Kill(IN) : IN)
 *
 *       - **Properties of f_b^(m):**
 *           - The transfer function is monotonic as it either returns a
 *             constant value, forwards the input, or applies a monotonic
 *             operation.
 *           - Within a basic block:
 *               - If there are no function calls and the memory operation m
 *                 does not occur, the input is directly forwarded.
 *               - If the memory operation m ∈ BB, the transfer function returns
 *                 the constant 1 or ⊤ depending on whether m  occurs after
 *                 the last function call c.
 *               - If the basic block B contains function calls and m is
 *                 reachable, then the \( m \) is deactivated to ⊤.
 */
/// Similar static dataflow analysis can be found in
/// SCCPInstVisitor@SCCPSolver.cpp.
class ActiveMopAnalysis {
private:
  /// @brief Type aliases for readability.
  using MopID = unsigned;
  using BitVectorSet = BitVector;

  /// @brief The lattice element representing the abstract attribute of a MOP.
  /// In fact, we use two {0,1}^m to represent the domain L^m, for analytical
  /// efficiency reasons. L = {unreachable, reachable, not-live}.
  struct Lattice {
    // 00 -> ⊥, unreachable
    // 10 -> ~, reachable and live
    // 11 -> ⊤, reachable but not live
    BitVectorSet Reachable;
    BitVectorSet NotActive;

    /*
    If applied to TSan, we must consider the memory barriers.
    i.e., RELEASE/ACQUIRE.
    Therefore, for TSan, we extend this lattice
    from
        ⊥ -> ~ -> ⊤
    to
        ⊥ -> ~ -> A -> ⊤
              |-> R ->|
    where A represents any acquire barrier, and R represents any release
    barrier. before this program point.

    If KillingI dominates DeadI (Killing --> Dead),
    DeadI could not be eliminated if there is a release barrier between them,
    as the DeadI is not guarded by the release barrier.
    Otherwise, FN arises if other threads access the same memory location
    after crossing the relevant acquire barrier.
    Similarly, if KillingI post-dominate DeadI (Dead --> Killing).
    DeadI could not be eliminated if there is an acquire barrier between
    them, as the DeadI is not guarded by the acquire barrier.
    */
    BitVectorSet AnyAcq;
    BitVectorSet AnyRel;
    bool UsedForTsan;

    Lattice(unsigned NumMops, bool UsedForTsan);

    // Inplace version of kill function.
    void kill();
    void killAcq();
    void killRel();

    /// a = a ∨ b, to be the least upper bound.
    void join(const Lattice &Other);

    bool operator==(const Lattice &Other) const;

    inline bool isActive(MopID Id) const {
      return Reachable.test(Id) && !NotActive.test(Id);
    }

    inline bool isActive(MopID Id, bool Acq) const {
      return isActive(Id) &&
             ((Acq && !AnyAcq.test(Id)) || (!Acq && !AnyRel.test(Id)));
    }
  };

  /// @brief A structure to store the invariant of a basic block.
  struct BlockInvariant {
    const BitVectorSet UseGen;
    const BitVectorSet NotUseGen;
    const BitVectorSet Gen;
    const BitVectorSet GenAcq;
    const BitVectorSet GenRel;
    const bool ContainsCall; // UseKill
    const bool ContainsAcq;  // UseKillAcq
    const bool ContainsRel;  // UseKillRel
  };
  /// @brief A structure to store the IN and OUT sets of a basic block.
  struct BlockInfo {
    Lattice IN;
    Lattice OUT;
    const BlockInvariant Invariant;
  };

private:
  /// @brief Extracts all MOPs from the given function.
  SmallVector<const Instruction *, 64> extractMOPs(const Function &F) const;

  /// @brief Initializes the MOP list and mapping.
  void initializeMopMaps();

  /// @brief Retrieves the unique MopID for a given instruction.
  MopID getMopId(const Instruction *I) const;

  /// @brief Initializes the BlockInfoMap with IN and OUT sets.
  void initializeBlockInfo(const Function &F);

  /// @brief Initializes the BlockInfo for a given basic block.
  BlockInfo getInitializedBlockInfo(const BasicBlock &BB) const;

  /// @brief Retrieves the BlockInfo for a given basic block.
  const BlockInfo &getBlockInfo(const BasicBlock *BB) const;
  BlockInfo &getBlockInfo(const BasicBlock *BB);

  /// @brief Checks if a Mop is active after the given basic block.
  bool isMopActiveAfter(MopID Mop, const BasicBlock *BB, const bool Acq) const;

  /// @brief Checks if a Mop is active before the given basic block.
  bool isMopActiveBefore(MopID Mop, const BasicBlock *BB, const bool Acq) const;

  void printInfo(const BlockInfo &info, raw_ostream &OS) const;

  /// @brief Updates the BlockInfo for a given basic block, and returns true if
  /// the state changed.
  /// Apply merge function and transfer function on the given basic block.
  bool updateInfo(const BasicBlock *BB);

  /// OUT = UseGen? Gen : (HasCall? Kill(IN) : IN)
  Lattice transfer(const BlockInfo &Info) const;

  /// @brief Retrieves the list of memory operations (MOPs).
  const SmallVector<const Instruction *, 64> &getMOPList() const;

  /// @brief Executes the dataflow analysis.
  void dataflowAnalyze();

  bool suitableToAnlyze();

public:
  /// @brief Constructor.
  ///
  /// @param F The LLVM function to analyze.
  /// @param MOPs An optional externally provided list of MOP instructions.
  ActiveMopAnalysis(Function &F,
                    const SmallVectorImpl<const Instruction *> &MOPs,
                    bool UsedForTsan);

  /// @brief Constructor that automatically extracts MOPs from the function.
  ///
  /// @param F The LLVM function to analyze.
  ActiveMopAnalysis(Function &F, bool UsedForTsan);

  /// @brief Determines if a MOP is reachable from one instruction to another.
  ///
  /// @param From The starting instruction.
  /// @param To The target instruction.
  /// @param IsToDead True if the target instruction is used for dead I.
  /// @return True if the MOP is reachable, false otherwise.
  bool isOneMopActiveToAnother(const Instruction *From, const Instruction *To,
                               bool IsToDead) const;

  /// @brief Prints the analysis results to the specified raw_ostream.
  ///
  /// @param OS The output stream to print to.
  void printAnalysis(raw_ostream &OS);

private:
  /// @brief A mapping from instructions to unique MOP IDs.
  DenseMap<const Instruction *, MopID> MopMap;

  /// @brief A list of all memory operations (MOPs) in the function.
  SmallVector<const Instruction *, 64> MopList;

  /// @brief A mapping from basic blocks to their BlockInfo.
  DenseMap<const BasicBlock *, BlockInfo> BlockInfoMap;

  const Function &F;

  bool Analyzed;
  const bool UsedForTsan;
};
} // namespace __xsan