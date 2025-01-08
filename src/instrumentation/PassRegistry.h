#pragma once

#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassPlugin.h"

namespace __xsan {

void registerAsanForClangAndOpt(llvm::PassBuilder &PB);
void registerTsanForClangAndOpt(llvm::PassBuilder &PB);

class SubSanitizers : public llvm::ModulePassManager {
public:
  /// Run all of the sanitizer passes on the module, and preserve all analysises
  /// between them.
  llvm::PreservedAnalyses run(llvm::Module &IR,
                              llvm::ModuleAnalysisManager &AM);
};

SubSanitizers loadSubSanitizers();

} // namespace __xsan