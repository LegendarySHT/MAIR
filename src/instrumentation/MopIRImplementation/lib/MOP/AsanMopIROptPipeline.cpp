// AsanRequireAnalysisPass 中的实际调度顺序见 AddressSanitizer.cpp：
// collectTargets → tryRunAsanMopIRRedundancyPhase（MopIRImpl::PassManager
// 跑 RedundantCheckEliminationPass）→ runAsanMopIRLoopRelocationPhase
//（LLVM::LLVMPassManager 跑 LoopInvariantRelocationLLVMWrapperPass）。

#include "MOP/AsanMopIROptPipeline.h"

#if defined(MAIR_USE_MOPIR_ASAN_LOOP_RELOC)
#include "LLVM/LLVMPassManager.h"
#include "MOP/Passes/LoopInvariantRelocationLLVMWrapperPass.h"
#endif
#if defined(MAIR_USE_MOPIR_ASAN_REDUNDANCY)
#include "MOP/AsanRedundancyAdapter.h"
#endif

#include "llvm/IR/Function.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/GlobalValue.h"

namespace MopIRImpl {
namespace MOP {

void runAsanMopIRLoopRelocationPhase(llvm::Function &F,
                                     llvm::FunctionAnalysisManager &FAM,
                                     bool mopirLoopRelocationStrategyEnabled,
                                     llvm::StringRef asDebugFuncName) {
#if !defined(MAIR_USE_MOPIR_ASAN_LOOP_RELOC)
  (void)F;
  (void)FAM;
  (void)mopirLoopRelocationStrategyEnabled;
  (void)asDebugFuncName;
  return;
#else
  if (!mopirLoopRelocationStrategyEnabled)
    return;
  if (F.isDeclaration() || F.empty())
    return;
  if (!F.hasFnAttribute(llvm::Attribute::SanitizeAddress))
    return;
  if (F.hasFnAttribute(llvm::Attribute::DisableSanitizerInstrumentation))
    return;
  if (F.getLinkage() == llvm::GlobalValue::AvailableExternallyLinkage)
    return;
  if (F.getName().startswith("__asan_"))
    return;
  if (!asDebugFuncName.empty() && asDebugFuncName == F.getName())
    return;
  MopIRImpl::LLVM::LLVMPassManager LPM(&FAM, nullptr);
  LPM.addPass(
      std::make_unique<MopIRImpl::MOP::LoopInvariantRelocationLLVMWrapperPass>());
  LPM.run(F, nullptr);
#endif
}

bool tryRunAsanMopIRRedundancyPhase(
    llvm::Function &F, llvm::FunctionAnalysisManager &FAM,
    llvm::ModuleAnalysisManager &MAM,
    llvm::ArrayRef<const llvm::Instruction *> TmpInsts,
    llvm::SmallVectorImpl<const llvm::Instruction *> &Out) {
#if !defined(MAIR_USE_MOPIR_ASAN_REDUNDANCY)
  (void)F;
  (void)FAM;
  (void)MAM;
  (void)TmpInsts;
  (void)Out;
  return false;
#else
  return runAsanRedundancyReduction(F, FAM, MAM, TmpInsts, Out);
#endif
}

} // namespace MOP
} // namespace MopIRImpl
