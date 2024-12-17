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
#include "llvm/Transforms/Instrumentation/AddressSanitizerOptions.h"
#include "xsan_common.h"

namespace llvm {
class Module;
class raw_ostream;




/// This pass tag each function with proper sanitizer attribute.
class AttributeTaggingPass
    : public PassInfoMixin<AttributeTaggingPass> {
public:
  AttributeTaggingPass(SanitizerType);
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  static bool isRequired() { return true; }
private:
  SanitizerType _sanTy;
};


} // namespace llvm
