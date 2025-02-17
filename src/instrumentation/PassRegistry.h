#pragma once

#include "llvm/IR/PassManager.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

namespace llvm {
struct AddressSanitizerOptions;
enum class AsanDtorKind;
} // namespace llvm

namespace __xsan {

class SubSanitizers : public llvm::ModulePassManager {
private:
  SubSanitizers() = default;

public:
  static SubSanitizers loadSubSanitizers();
  /// Run all of the sanitizer passes on the module, and preserve all analysises
  /// between them.
  llvm::PreservedAnalyses run(llvm::Module &IR,
                              llvm::ModuleAnalysisManager &AM);
};

/*
 A straightforward approach is to reuse the O1/O2/O3 optimization pipeline,
 which performs a excellent optimization on the code. However, this approach
 will bring compilation overhead, and most optimizations are repeated
 pointlessly.

 We just need to add some optimization passes designated for sanitizers!

 Therefore, we manually selected some low-cost and effective optimization
 passes.
 */
llvm::ModulePassManager
createPostOptimizationPasses(llvm::OptimizationLevel Level);

void obtainAsanPassArgs(llvm::AddressSanitizerOptions &Opts, bool &UseGlobalGC,
                        bool &UseOdrIndicator,
                        llvm::AsanDtorKind &DestructorKind);

void registerAnalysisForXsan(llvm::PassBuilder &PB);

// -- The following funtcions are implemented in XXXSanitizer.cpp --------
/// Because the structure, indicating targets to instrument, depends on the
/// interior type in each XXXSanitizer.cpp
struct AsanToInstrument;
struct TsanToInstrument;
void registerAnalysisForAsan(llvm::PassBuilder &PB);
void registerAnalysisForTsan(llvm::PassBuilder &PB);
void addAsanRequireAnalysisPass(SubSanitizers &MPM,
                                llvm::FunctionPassManager &FPM);
void addTsanRequireAnalysisPass(SubSanitizers &MPM,
                                llvm::FunctionPassManager &FPM);

// --------- Implemented in PassRegistry.cpp -----------
void registerAsanForClangAndOpt(llvm::PassBuilder &PB);
void registerTsanForClangAndOpt(llvm::PassBuilder &PB);

// --------- Use for XSan's optimization ---------------
bool shouldTsanOptimizeLoadStores();
bool shouldAsanOptimizeLoadStores();
bool shouldAsanPoisonInternalGlobals();
} // namespace __xsan