#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instruction.h"

namespace __xsan {

/// Mark an instruction as delegated to XSan.
void markAsDelegatedToXsan(llvm::Instruction &I);

/// Check if an instruction is delegated to XSan.
bool isDelegatedToXsan(const llvm::Instruction &I);

inline bool isNoSanitize(const llvm::Instruction &I) {
  return I.hasMetadata(llvm::LLVMContext::MD_nosanitize);
}

bool shouldSkip(const llvm::Instruction &I);

const llvm::Value *extractAddrFromLoadStoreInst(const llvm::Instruction &I);

/// Direct migrated from new version LLVM, which introduced by this PR:
/// https://github.com/llvm/llvm-project/pull/99509
const llvm::Value *getUnderlyingObjectAggressive(const llvm::Value *V);

bool addrPointsToConstantDataAggressive(const llvm::Value *Addr);

/// `alloc` is handled in other place, this function is for other cases.
/// 1. `byval` pointer argument
/// 2. `noalias` call return value
/// 3. `alloc` stack object
/// Note that `noalias` argument should have been a function local, but because
/// it belongs to capture by return in the context of TSan, it is not included.
bool isUncapturedFuncLocal(const llvm::Value &Addr);

// Check whether the Instruction is on the top of the BB (ignore debug/phi
// instruction). If so, just return its parent BB address. Otherwise, split the
// BB on the location of this instruction, and return the new split BB address.
llvm::BlockAddress *getBlockAddressOfInstruction(llvm::Instruction &I,
                                                 llvm::DominatorTree *DT,
                                                 llvm::LoopInfo *LI,
                                                 llvm::MemorySSAUpdater *MSSAU);

} // namespace __xsan
