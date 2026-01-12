#ifndef MOP_IR_IMPLEMENTATION_LLVM_PASS_CONTEXT_H
#define MOP_IR_IMPLEMENTATION_LLVM_PASS_CONTEXT_H

#ifdef MOP_IR_USE_LLVM

#include "PassContext.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Support/ErrorHandling.h"
#include <memory>

namespace MopIRImpl {
namespace LLVM {

/**
 * LLVM Pass Context
 * 
 * 扩展基础 PassContext，集成 LLVM 的 AnalysisManager
 * 可以自动从 LLVM 获取分析结果
 */
class LLVMPassContext : public MopIRImpl::PassContext {
private:
  llvm::FunctionAnalysisManager* FAM;
  llvm::ModuleAnalysisManager* MAM;
  llvm::Function* CurrentFunction;
  llvm::Module* CurrentModule;
  
public:
  LLVMPassContext() 
    : FAM(nullptr), MAM(nullptr), CurrentFunction(nullptr), CurrentModule(nullptr) {}
  
  // 从 FunctionAnalysisManager 构造
  explicit LLVMPassContext(llvm::FunctionAnalysisManager& fam, llvm::Function& F)
    : FAM(&fam), MAM(nullptr), CurrentFunction(&F), CurrentModule(nullptr) {}
  
  // 从 ModuleAnalysisManager 构造
  explicit LLVMPassContext(llvm::ModuleAnalysisManager& mam, llvm::Module& M)
    : FAM(nullptr), MAM(&mam), CurrentFunction(nullptr), CurrentModule(&M) {}
  
  // 同时提供 Function 和 Module 分析管理器
  LLVMPassContext(llvm::FunctionAnalysisManager& fam, 
                  llvm::ModuleAnalysisManager& mam,
                  llvm::Function& F)
    : FAM(&fam), MAM(&mam), CurrentFunction(&F), 
      CurrentModule(F.getParent()) {}
  
  // 设置当前函数
  void setCurrentFunction(llvm::Function& F) {
    CurrentFunction = &F;
    if (!CurrentModule) {
      CurrentModule = F.getParent();
    }
  }
  
  // 设置当前模块
  void setCurrentModule(llvm::Module& M) {
    CurrentModule = &M;
  }
  
  // 获取 LLVM 分析结果（自动从 AnalysisManager 获取）
  
  /**
   * 获取 AAResults（别名分析）
   */
  llvm::AAResults& getAAResults() {
    if (!CurrentFunction || !FAM) {
      // 如果没有 Function，尝试从 Context 缓存获取
      if (!CurrentModule) {
        llvm::report_fatal_error("No function or module available for AAResults");
      }
      return getOrCompute<llvm::AAResults>([this]() {
        llvm::TargetLibraryInfoWrapperPass TLIWP;
        TLIWP.getTLI(*CurrentModule);
        return llvm::AAResults(TLIWP.getTLI(*CurrentModule));
      });
    }
    
    // 从 LLVM AnalysisManager 获取
    return FAM->getResult<llvm::AAManager>(*CurrentFunction);
  }
  
  /**
   * 获取 DominatorTree（支配树）
   */
  llvm::DominatorTree& getDominatorTree() {
    if (!CurrentFunction || !FAM) {
      if (!CurrentFunction) {
        llvm::report_fatal_error("No function available for DominatorTree");
      }
      return getOrCompute<llvm::DominatorTree>([this]() {
        return llvm::DominatorTree(*CurrentFunction);
      });
    }
    return FAM->getResult<llvm::DominatorTreeAnalysis>(*CurrentFunction);
  }
  
  /**
   * 获取 PostDominatorTree（后支配树）
   */
  llvm::PostDominatorTree& getPostDominatorTree() {
    if (!CurrentFunction || !FAM) {
      if (!CurrentFunction) {
        llvm::report_fatal_error("No function available for PostDominatorTree");
      }
      return getOrCompute<llvm::PostDominatorTree>([this]() {
        return llvm::PostDominatorTree(*CurrentFunction);
      });
    }
    return FAM->getResult<llvm::PostDominatorTreeAnalysis>(*CurrentFunction);
  }
  
  /**
   * 获取 LoopInfo（循环信息）
   */
  llvm::LoopInfo& getLoopInfo() {
    if (!CurrentFunction || !FAM) {
      if (!CurrentFunction) {
        llvm::report_fatal_error("No function available for LoopInfo");
      }
      return getOrCompute<llvm::LoopInfo>([this]() {
        return llvm::LoopInfo(*CurrentFunction);
      });
    }
    return FAM->getResult<llvm::LoopAnalysis>(*CurrentFunction);
  }
  
  /**
   * 获取 ScalarEvolution（标量演化）
   */
  llvm::ScalarEvolution& getScalarEvolution() {
    if (!CurrentFunction || !FAM) {
      if (!CurrentFunction || !CurrentModule) {
        llvm::report_fatal_error("No function or module available for ScalarEvolution");
      }
      return getOrCompute<llvm::ScalarEvolution>([this]() {
        llvm::TargetLibraryInfoWrapperPass TLIWP;
        TLIWP.getTLI(*CurrentFunction);
        return llvm::ScalarEvolution(*CurrentFunction, 
                                      CurrentModule->getDataLayout(),
                                      TLIWP.getTLI(*CurrentFunction));
      });
    }
    return FAM->getResult<llvm::ScalarEvolutionAnalysis>(*CurrentFunction);
  }
  
  /**
   * 获取 TargetLibraryInfo（目标库信息）
   */
  const llvm::TargetLibraryInfo& getTargetLibraryInfo() {
    if (!CurrentFunction || !FAM) {
      if (!CurrentFunction) {
        llvm::report_fatal_error("No function available for TargetLibraryInfo");
      }
      return *getOrCompute<const llvm::TargetLibraryInfo*>([this]() {
        llvm::TargetLibraryInfoWrapperPass TLIWP;
        TLIWP.getTLI(*CurrentFunction);
        return &TLIWP.getTLI(*CurrentFunction);
      });
    }
    return FAM->getResult<llvm::TargetLibraryAnalysis>(*CurrentFunction);
  }
  
  /**
   * 获取 DataLayout（数据布局）
   */
  const llvm::DataLayout& getDataLayout() {
    if (!CurrentModule) {
      llvm::report_fatal_error("No module available for DataLayout");
    }
    return CurrentModule->getDataLayout();
  }
  
  // 访问器
  llvm::FunctionAnalysisManager* getFAM() const { return FAM; }
  llvm::ModuleAnalysisManager* getMAM() const { return MAM; }
  llvm::Function* getCurrentFunction() const { return CurrentFunction; }
  llvm::Module* getCurrentModule() const { return CurrentModule; }
  
  /**
   * 使 LLVM 分析结果失效
   * 
   * 这会同时清除缓存和通知 LLVM AnalysisManager
   */
  template<typename AnalysisT>
  void invalidateLLVMAnalysis() {
    invalidate<AnalysisT>();
    
    if (FAM && CurrentFunction) {
      // 通知 LLVM AnalysisManager 失效
      // 注意：这需要根据具体的 AnalysisT 类型来处理
      // 这里提供一个通用接口
    }
  }
};

} // namespace LLVM
} // namespace MopIRImpl

#endif // MOP_IR_USE_LLVM

#endif // MOP_IR_IMPLEMENTATION_LLVM_PASS_CONTEXT_H

