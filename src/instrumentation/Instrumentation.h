//===------------Instrumentation.h - TransformUtils Infrastructure --------===//
//
// This file is to provide some instrumentation utilities APIs.
// These APIs usually come from the subsequent versions of LLVM, i.e., LLVM 15+.
//
//===----------------------------------------------------------------------===//
#pragma once

#include "llvm/ADT/StringRef.h"

namespace llvm {
class Module;
}

namespace __xsan {
/// Check if module has flag attached, if not add the flag.
bool checkIfAlreadyInstrumented(llvm::Module &M, llvm::StringRef Flag);
} // namespace __xsan