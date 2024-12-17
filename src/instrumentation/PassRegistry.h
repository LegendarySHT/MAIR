#pragma once

#include "llvm/Passes/PassPlugin.h"


namespace llvm {
class PassBuilder;
}

namespace __xsan {

void registerAsanForClangAndOpt(llvm::PassBuilder &PB);
void registerTsanForClangAndOpt(llvm::PassBuilder &PB);

void registerXsanForClangAndOpt(llvm::PassBuilder &PB);
} // namespace __xsan