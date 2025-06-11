//===----------------------------------------------------------------------===//
//
// This file is to define a transformation pass that adds attribute tags to
// functions and instructions. Because the sanitizer passes only instrument
// those functions with appropriate attribute.
// - AddressSanitizer: attribute `sanitize_address`
// - ThreadSanitizer: attribute `sanitize_address`
//
//===----------------------------------------------------------------------===//
#pragma once

#include "llvm/IR/PassManager.h"

namespace llvm {
class Module;

/// This pass tag each instruction with `xsan.ubsan` metadata.
class UbsanInstTaggingPass : public PassInfoMixin<UbsanInstTaggingPass> {
public:
  UbsanInstTaggingPass();
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  static bool isRequired() { return true; }
};

} // namespace llvm
