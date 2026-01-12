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
  bool optimize(MopIRImpl::IRUnit& IR, MopIRImpl::PassContext& Context) override {
    // 类型转换
    auto* funcIR = dynamic_cast<FunctionIRUnit*>(&IR);
    if (!funcIR) {
      return false;
    }
    
    // 转换 Context
    auto* llvmContext = dynamic_cast<LLVMPassContext*>(&Context);
    if (!llvmContext) {
      // 如果不是 LLVM Context，尝试创建临时 Context
      // 这需要 FunctionAnalysisManager，暂时返回 false
      return false;
    }
    
    // 设置当前函数
    llvmContext->setCurrentFunction(funcIR->getFunction());
    
    // 调用 LLVM 特定的优化方法
    return optimizeFunction(funcIR->getFunction(), *llvmContext);
  }
  
  /**
   * LLVM Function 优化方法（子类实现）
   */
  virtual bool optimizeFunction(llvm::Function& F, LLVMPassContext& Context) = 0;
  
  const char* getName() const override {
    return getLLVMName();
  }
  
  /**
   * LLVM Pass 名称（子类实现）
   */
  virtual const char* getLLVMName() const = 0;
};

/**
 * LLVM Module Pass 基类
 * 
 * 方便编写针对 LLVM Module 的 Pass
 */
class ModulePass : public MopIRImpl::Pass {
public:
  bool optimize(MopIRImpl::IRUnit& IR, MopIRImpl::PassContext& Context) override {
    // 类型转换
    auto* modIR = dynamic_cast<ModuleIRUnit*>(&IR);
    if (!modIR) {
      return false;
    }
    
    // 转换 Context
    auto* llvmContext = dynamic_cast<LLVMPassContext*>(&Context);
    if (!llvmContext) {
      return false;
    }
    
    // 设置当前模块
    llvmContext->setCurrentModule(modIR->getModule());
    
    // 调用 LLVM 特定的优化方法
    return optimizeModule(modIR->getModule(), *llvmContext);
  }
  
  /**
   * LLVM Module 优化方法（子类实现）
   */
  virtual bool optimizeModule(llvm::Module& M, LLVMPassContext& Context) = 0;
  
  const char* getName() const override {
    return getLLVMName();
  }
  
  /**
   * LLVM Pass 名称（子类实现）
   */
  virtual const char* getLLVMName() const = 0;
};

/**
 * LLVM Pass 适配器
 * 
 * 将现有的 LLVM Pass（继承自 PassInfoMixin）适配到我们的 Pass Manager
 */
template<typename LLVMPassT>
class LLVMPassAdapter : public MopIRImpl::Pass {
private:
  LLVMPassT LLVMPass;
  llvm::FunctionAnalysisManager* FAM;
  llvm::ModuleAnalysisManager* MAM;
  
public:
  LLVMPassAdapter(llvm::FunctionAnalysisManager* fam = nullptr,
                  llvm::ModuleAnalysisManager* mam = nullptr)
    : FAM(fam), MAM(mam) {}
  
  bool optimize(MopIRImpl::IRUnit& IR, MopIRImpl::PassContext& Context) override {
    // 尝试转换为 FunctionIRUnit
    if (auto* funcIR = dynamic_cast<FunctionIRUnit*>(&IR)) {
      if (!FAM) {
        return false;
      }
      
      auto* llvmContext = dynamic_cast<LLVMPassContext*>(&Context);
      if (!llvmContext) {
        return false;
      }
      
      llvmContext->setCurrentFunction(funcIR->getFunction());
      
      // 运行 LLVM Pass
      llvm::PreservedAnalyses PA = LLVMPass.run(funcIR->getFunction(), *FAM);
      
      // 检查是否修改了 IR
      return !PA.areAllPreserved();
    }
    
    // 尝试转换为 ModuleIRUnit
    if (auto* modIR = dynamic_cast<ModuleIRUnit*>(&IR)) {
      if (!MAM) {
        return false;
      }
      
      auto* llvmContext = dynamic_cast<LLVMPassContext*>(&Context);
      if (!llvmContext) {
        return false;
      }
      
      llvmContext->setCurrentModule(modIR->getModule());
      
      // 运行 LLVM Pass
      llvm::PreservedAnalyses PA = LLVMPass.run(modIR->getModule(), *MAM);
      
      return !PA.areAllPreserved();
    }
    
    return false;
  }
  
  const char* getName() const override {
    return LLVMPass.name();
  }
};

} // namespace LLVM
} // namespace MopIRImpl

#endif // MOP_IR_USE_LLVM

#endif // MOP_IR_IMPLEMENTATION_LLVM_PASS_H

