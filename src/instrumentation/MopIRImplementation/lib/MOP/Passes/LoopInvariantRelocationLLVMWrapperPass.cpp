#include "MOP/Passes/LoopInvariantRelocationLLVMWrapperPass.h"

#ifdef MOP_IR_USE_LLVM

#include "MOP/LoopInvariantRelocationAdapter.h"
#include "LLVM/LLVMIRUnit.h"
#include "LLVM/LLVMPassContext.h"

namespace MopIRImpl {
namespace MOP {

bool LoopInvariantRelocationLLVMWrapperPass::optimize(MopIRImpl::IRUnit &IR,
                                                      MopIRImpl::PassContext &Ctx) {
  if (IR.getIRUnitKind() != MopIRImpl::IRUnit::Kind::LLVMFunction)
    return false;
  if (Ctx.getPassContextKind() != MopIRImpl::PassContext::Kind::LLVM)
    return false;
  auto &FuncU = static_cast<MopIRImpl::LLVM::FunctionIRUnit &>(IR);
  auto &LC = static_cast<MopIRImpl::LLVM::LLVMPassContext &>(Ctx);
  llvm::FunctionAnalysisManager *FAM = LC.getFAM();
  if (!FAM)
    return false;
  runLoopInvariantRelocationOnly(FuncU.getFunction(), *FAM);
  return true;
}

} // namespace MOP
} // namespace MopIRImpl

#endif // MOP_IR_USE_LLVM
