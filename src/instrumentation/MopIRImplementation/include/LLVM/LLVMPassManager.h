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
 * 
 * 扩展基础 PassManager，提供 LLVM 特定的功能：
 * - 自动管理 LLVM AnalysisManager
 * - 支持 LLVM PreservedAnalyses
 * - 与 LLVM PassBuilder 集成
 */
class LLVMPassManager : public MopIRImpl::PassManager {
private:
  llvm::FunctionAnalysisManager* FAM;
  llvm::ModuleAnalysisManager* MAM;
  bool PreserveAnalyses;  // 是否在 Pass 之间保留分析结果
  
public:
  LLVMPassManager(llvm::FunctionAnalysisManager* fam = nullptr,
                  llvm::ModuleAnalysisManager* mam = nullptr,
                  bool preserveAnalyses = true)
    : FAM(fam), MAM(mam), PreserveAnalyses(preserveAnalyses) {}
  
  /**
   * 运行 Pass 流水线（LLVM 版本）
   * 
   * 自动创建 LLVMPassContext 并管理分析结果
   */
  bool run(llvm::Function& F, MopIRImpl::PassContext* Context = nullptr) {
    FunctionIRUnit FuncIR(F);
    
    // 创建或使用提供的 Context
    LLVMPassContext llvmContext;
    if (Context) {
      // 如果提供了 Context，尝试转换为 LLVMPassContext
      auto* llvmCtx = dynamic_cast<LLVMPassContext*>(Context);
      if (llvmCtx) {
        llvmContext = std::move(*llvmCtx);
      }
    }
    
    // 设置 AnalysisManager
    if (FAM) {
      llvmContext = LLVMPassContext(*FAM, F);
    }
    
    // 运行基础 PassManager
    return MopIRImpl::PassManager::run(FuncIR, &llvmContext);
  }
  
  /**
   * 运行 Pass 流水线（Module 版本）
   */
  bool run(llvm::Module& M, MopIRImpl::PassContext* Context = nullptr) {
    ModuleIRUnit ModIR(M);
    
    // 创建或使用提供的 Context
    LLVMPassContext llvmContext;
    if (Context) {
      auto* llvmCtx = dynamic_cast<LLVMPassContext*>(Context);
      if (llvmCtx) {
        llvmContext = std::move(*llvmCtx);
      }
    }
    
    // 设置 AnalysisManager
    if (MAM) {
      llvmContext = LLVMPassContext(*MAM, M);
    }
    
    return MopIRImpl::PassManager::run(ModIR, &llvmContext);
  }
  
  /**
   * 运行 Pass 流水线（带 PreservedAnalyses）
   * 
   * 注意：这个方法需要 Pass 返回 PreservedAnalyses 信息
   * 目前简化实现，返回 all() 或 none()
   */
  llvm::PreservedAnalyses runWithPreservedAnalyses(
      llvm::Function& F, 
      MopIRImpl::PassContext* Context = nullptr) {
    
    FunctionIRUnit FuncIR(F);
    LLVMPassContext llvmContext;
    
    if (FAM) {
      llvmContext = LLVMPassContext(*FAM, F);
    }
    
    if (Context) {
      auto* llvmCtx = dynamic_cast<LLVMPassContext*>(Context);
      if (llvmCtx) {
        llvmContext = std::move(*llvmCtx);
      }
    }
    
    // 运行基础 PassManager，它会返回是否修改了 IR
    bool modified = MopIRImpl::PassManager::run(FuncIR, &llvmContext);
    
    // 根据修改情况返回 PreservedAnalyses
    // 注意：这是简化实现，实际应该让每个 Pass 返回 PreservedAnalyses
    if (modified) {
      return llvm::PreservedAnalyses::none();
    } else {
      return llvm::PreservedAnalyses::all();
    }
  }
  
  // 访问器
  void setFAM(llvm::FunctionAnalysisManager* fam) { FAM = fam; }
  void setMAM(llvm::ModuleAnalysisManager* mam) { MAM = mam; }
  void setPreserveAnalyses(bool preserve) { PreserveAnalyses = preserve; }
  
  llvm::FunctionAnalysisManager* getFAM() const { return FAM; }
  llvm::ModuleAnalysisManager* getMAM() const { return MAM; }
};

} // namespace LLVM
} // namespace MopIRImpl

#endif // MOP_IR_USE_LLVM

#endif // MOP_IR_IMPLEMENTATION_LLVM_PASS_MANAGER_H

