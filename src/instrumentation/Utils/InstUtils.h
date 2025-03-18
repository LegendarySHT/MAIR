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
} // namespace __xsan