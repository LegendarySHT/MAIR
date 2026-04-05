#ifndef MOP_IR_IMPLEMENTATION_LLVM_PASS_MANAGER_H
#define MOP_IR_IMPLEMENTATION_LLVM_PASS_MANAGER_H

#ifdef MOP_IR_USE_LLVM

#include "PassManager.h"
#include "LLVM/LLVMPassContext.h"
#include "LLVM/LLVMIRUnit.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"

namespace MopIRImpl {
namespace LLVM {

/**
 * LLVM Pass Manager
 */
class LLVMPassManager : public MopIRImpl::PassManager {
private:
  llvm::FunctionAnalysisManager *FAM;
  llvm::ModuleAnalysisManager *MAM;
  bool PreserveAnalyses;

public:
  LLVMPassManager(llvm::FunctionAnalysisManager *fam = nullptr,
                  llvm::ModuleAnalysisManager *mam = nullptr,
                  bool preserveAnalyses = true)
      : FAM(fam), MAM(mam), PreserveAnalyses(preserveAnalyses) {}

  bool run(llvm::Function &F, MopIRImpl::PassContext *Context = nullptr) {
    FunctionIRUnit FuncIR(F);

    LLVMPassContext llvmContext;
    if (Context &&
        Context->getPassContextKind() == MopIRImpl::PassContext::Kind::LLVM) {
      llvmContext = std::move(*static_cast<LLVMPassContext *>(Context));
    }

    if (FAM) {
      llvmContext = LLVMPassContext(*FAM, F);
    }

    return MopIRImpl::PassManager::run(FuncIR, &llvmContext);
  }

  bool run(llvm::Module &M, MopIRImpl::PassContext *Context = nullptr) {
    ModuleIRUnit ModIR(M);

    LLVMPassContext llvmContext;
    if (Context &&
        Context->getPassContextKind() == MopIRImpl::PassContext::Kind::LLVM) {
      llvmContext = std::move(*static_cast<LLVMPassContext *>(Context));
    }

    if (MAM) {
      llvmContext = LLVMPassContext(*MAM, M);
    }

    return MopIRImpl::PassManager::run(ModIR, &llvmContext);
  }

  llvm::PreservedAnalyses
  runWithPreservedAnalyses(llvm::Function &F,
                           MopIRImpl::PassContext *Context = nullptr) {

    FunctionIRUnit FuncIR(F);
    LLVMPassContext llvmContext;

    if (FAM) {
      llvmContext = LLVMPassContext(*FAM, F);
    }

    if (Context &&
        Context->getPassContextKind() == MopIRImpl::PassContext::Kind::LLVM) {
      llvmContext = std::move(*static_cast<LLVMPassContext *>(Context));
    }

    bool modified = MopIRImpl::PassManager::run(FuncIR, &llvmContext);

    if (modified) {
      return llvm::PreservedAnalyses::none();
    }
    return llvm::PreservedAnalyses::all();
  }

  void setFAM(llvm::FunctionAnalysisManager *fam) { FAM = fam; }
  void setMAM(llvm::ModuleAnalysisManager *mam) { MAM = mam; }
  void setPreserveAnalyses(bool preserve) { PreserveAnalyses = preserve; }

  llvm::FunctionAnalysisManager *getFAM() const { return FAM; }
  llvm::ModuleAnalysisManager *getMAM() const { return MAM; }
};

} // namespace LLVM
} // namespace MopIRImpl

#endif // MOP_IR_USE_LLVM

#endif // MOP_IR_IMPLEMENTATION_LLVM_PASS_MANAGER_H
