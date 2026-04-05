#ifndef MOP_IR_IMPLEMENTATION_LLVM_PASS_H
#define MOP_IR_IMPLEMENTATION_LLVM_PASS_H

#ifdef MOP_IR_USE_LLVM

#include "Pass.h"
#include "LLVM/LLVMPassContext.h"
#include "LLVM/LLVMIRUnit.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"

namespace MopIRImpl {
namespace LLVM {

/**
 * LLVM Function Pass 基类
 *
 * 方便编写针对 LLVM Function 的 Pass
 * 自动处理类型转换和 LLVM 分析结果的获取
 */
class FunctionPass : public MopIRImpl::Pass {
public:
  bool optimize(MopIRImpl::IRUnit &IR, MopIRImpl::PassContext &Context) override {
    if (IR.getIRUnitKind() != MopIRImpl::IRUnit::Kind::LLVMFunction)
      return false;

    if (Context.getPassContextKind() != MopIRImpl::PassContext::Kind::LLVM)
      return false;

    auto &funcIR = static_cast<FunctionIRUnit &>(IR);
    auto &llvmContext = static_cast<LLVMPassContext &>(Context);

    llvmContext.setCurrentFunction(funcIR.getFunction());

    return optimizeFunction(funcIR.getFunction(), llvmContext);
  }

  /**
   * LLVM Function 优化方法（子类实现）
   */
  virtual bool optimizeFunction(llvm::Function &F, LLVMPassContext &Context) = 0;

  const char *getName() const override { return getLLVMName(); }

  virtual const char *getLLVMName() const = 0;
};

/**
 * LLVM Module Pass 基类
 */
class ModulePass : public MopIRImpl::Pass {
public:
  bool optimize(MopIRImpl::IRUnit &IR, MopIRImpl::PassContext &Context) override {
    if (IR.getIRUnitKind() != MopIRImpl::IRUnit::Kind::LLVMModule)
      return false;

    if (Context.getPassContextKind() != MopIRImpl::PassContext::Kind::LLVM)
      return false;

    auto &modIR = static_cast<ModuleIRUnit &>(IR);
    auto &llvmContext = static_cast<LLVMPassContext &>(Context);

    llvmContext.setCurrentModule(modIR.getModule());

    return optimizeModule(modIR.getModule(), llvmContext);
  }

  virtual bool optimizeModule(llvm::Module &M, LLVMPassContext &Context) = 0;

  const char *getName() const override { return getLLVMName(); }

  virtual const char *getLLVMName() const = 0;
};

/**
 * LLVM Pass 适配器
 */
template <typename LLVMPassT> class LLVMPassAdapter : public MopIRImpl::Pass {
private:
  LLVMPassT LLVMPass;
  llvm::FunctionAnalysisManager *FAM;
  llvm::ModuleAnalysisManager *MAM;

public:
  LLVMPassAdapter(llvm::FunctionAnalysisManager *fam = nullptr,
                  llvm::ModuleAnalysisManager *mam = nullptr)
      : FAM(fam), MAM(mam) {}

  bool optimize(MopIRImpl::IRUnit &IR, MopIRImpl::PassContext &Context) override {
    if (Context.getPassContextKind() != MopIRImpl::PassContext::Kind::LLVM)
      return false;

    auto &llvmContext = static_cast<LLVMPassContext &>(Context);

    if (IR.getIRUnitKind() == MopIRImpl::IRUnit::Kind::LLVMFunction) {
      if (!FAM)
        return false;
      auto &funcIR = static_cast<FunctionIRUnit &>(IR);
      llvmContext.setCurrentFunction(funcIR.getFunction());
      llvm::PreservedAnalyses PA = LLVMPass.run(funcIR.getFunction(), *FAM);
      return !PA.areAllPreserved();
    }

    if (IR.getIRUnitKind() == MopIRImpl::IRUnit::Kind::LLVMModule) {
      if (!MAM)
        return false;
      auto &modIR = static_cast<ModuleIRUnit &>(IR);
      llvmContext.setCurrentModule(modIR.getModule());
      llvm::PreservedAnalyses PA = LLVMPass.run(modIR.getModule(), *MAM);
      return !PA.areAllPreserved();
    }

    return false;
  }

  const char *getName() const override { return LLVMPass.name(); }
};

} // namespace LLVM
} // namespace MopIRImpl

#endif // MOP_IR_USE_LLVM

#endif // MOP_IR_IMPLEMENTATION_LLVM_PASS_H
