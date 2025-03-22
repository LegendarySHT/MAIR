#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"

namespace __xsan {
using namespace llvm;

/// TODO: besides load/store, support mem intrinsics and atomic ops.
class StackObjRaceResult {
public:
  friend class StackObjRaceAnalysis;
  bool mightRace(const Instruction *Mop) const;
  /// Returns a list of MOPs lies in any path from any suspicious thread
  /// creation to any suspicious thread join. Two graph traversal algorithms can
  /// be used to find such MOPs.
  const SmallPtrSet<const Instruction *, 8> &
  getRacyMOPsForAllocaInst(const AllocaInst &Addr) const;

private:
  SmallDenseMap<const Value *, SmallPtrSet<const Instruction *, 8>, 8>
      mayRaces;
};

/*

** The following analysis is unproper! **

- Regarding TSan, for stack variables, any read or write before passing them to
the callee does not require instrumentation because there is no race condition.
  - Case 1: If the callee does not spawn a thread, there is no race condition.
  - Case 2: If the callee spawns a thread, the race condition can only occur
            with reads or writes after the call.
      - Case 1: If the caller does not join,
          - Case 1: If the callee joins, there can be no race condition in the
                    caller, so reads or writes after the call also do not need
                    to be checked.
          - Case 2: If the callee does not join, it is use-after-return, which
                    is undefined behavior (UB) and a bug. Therefore, the caller
                    should not check for race conditions.
              - Case 1: Even if the caller's caller joins, it is still
                        use-after-return.
              - Case 2: If the thread is leaked/detached, and it uses a stack
                        variable, it is also use-after-return (detached threads
                        should not reference stack variables).
      - Case 2: If the caller joins, there can be no race condition after the
                join, and the join can indicate that the callee spawned a
                thread.
- In summary, for the caller, only check stack variables between the calls that
might spawn and might join. Do not check at other times!
*/

class StackObjRaceAnalysis
    : public llvm::AnalysisInfoMixin<StackObjRaceAnalysis> {
  friend AnalysisInfoMixin<StackObjRaceAnalysis>;
  static llvm::AnalysisKey Key;

public:
  /// Provide the result typedef for this analysis pass.
  using Result = StackObjRaceResult;
  Result run(llvm::Function &F, llvm::FunctionAnalysisManager &FAM);
};
} // namespace __xsan