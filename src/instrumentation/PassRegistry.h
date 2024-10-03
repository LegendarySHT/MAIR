#pragma once

#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

namespace __xsan {

void registerAsanForClangAndOpt(llvm::PassBuilder &PB);
void registerTsanForClangAndOpt(llvm::PassBuilder &PB);

} // namespace __xsan