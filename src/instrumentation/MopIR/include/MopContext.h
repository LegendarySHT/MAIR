#ifndef XSAN_MOP_IR_CONTEXT_H
#define XSAN_MOP_IR_CONTEXT_H

#include "Mop.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

namespace __xsan {
namespace MopIR {

// MOP IR 上下文，统一管理优化和分析所需的所有信息
class MopContext {
private:
  llvm::Function& F;
  llvm::FunctionAnalysisManager* FAM;
  
  // 分析结果缓存
  llvm::AAResults* AA;
  llvm::DominatorTree* DT;
  llvm::PostDominatorTree* PDT;
  llvm::LoopInfo* LI;
  llvm::ScalarEvolution* SE;      // Scalar Evolution（用于访问模式分析）
  const llvm::TargetLibraryInfo* TLI;
  const llvm::DataLayout* DL;

public:
  // 从 FunctionAnalysisManager 构造
  MopContext(llvm::Function& Func, llvm::FunctionAnalysisManager& AnalysisManager)
    : F(Func), FAM(&AnalysisManager),
      AA(nullptr), DT(nullptr), PDT(nullptr), LI(nullptr), SE(nullptr),
      TLI(nullptr), DL(nullptr) {
    initialize();
  }
  
  // 直接提供分析结果构造（用于测试或特殊情况）
  MopContext(llvm::Function& Func, 
              llvm::AAResults& AAR,
              llvm::DominatorTree& DomTree,
              llvm::PostDominatorTree& PostDomTree,
              llvm::LoopInfo& LoopInfo,
              llvm::ScalarEvolution& ScalarEvo,
              const llvm::TargetLibraryInfo& TLIInfo)
    : F(Func), FAM(nullptr),
      AA(&AAR), DT(&DomTree), PDT(&PostDomTree), 
      LI(&LoopInfo), SE(&ScalarEvo), TLI(&TLIInfo),
      DL(&F.getParent()->getDataLayout()) {}

  // 访问器方法
  llvm::Function& getFunction() { return F; }
  const llvm::Function& getFunction() const { return F; }
  
  llvm::AAResults& getAAResults() {
    if (!AA && FAM) {
      AA = &FAM->getResult<llvm::AAManager>(F);
    }
    return *AA;
  }
  
  llvm::DominatorTree& getDominatorTree() {
    if (!DT && FAM) {
      DT = &FAM->getResult<llvm::DominatorTreeAnalysis>(F);
    }
    return *DT;
  }
  
  llvm::PostDominatorTree& getPostDominatorTree() {
    if (!PDT && FAM) {
      PDT = &FAM->getResult<llvm::PostDominatorTreeAnalysis>(F);
    }
    return *PDT;
  }
  
  llvm::LoopInfo& getLoopInfo() {
    if (!LI && FAM) {
      LI = &FAM->getResult<llvm::LoopAnalysis>(F);
    }
    return *LI;
  }
  
  llvm::ScalarEvolution& getScalarEvolution() {
    if (!SE && FAM) {
      SE = &FAM->getResult<llvm::ScalarEvolutionAnalysis>(F);
    }
    return *SE;
  }
  
  const llvm::TargetLibraryInfo& getTargetLibraryInfo() {
    if (!TLI && FAM) {
      TLI = &FAM->getResult<llvm::TargetLibraryAnalysis>(F);
    }
    return *TLI;
  }
  
  const llvm::DataLayout& getDataLayout() {
    if (!DL) {
      DL = &F.getParent()->getDataLayout();
    }
    return *DL;
  }
  
  llvm::FunctionAnalysisManager* getAnalysisManager() { return FAM; }

private:
  void initialize() {
    if (FAM) {
      // 延迟初始化，在首次访问时获取
    }
  }
};

} // namespace MopIR
} // namespace __xsan

#endif // XSAN_MOP_IR_CONTEXT_H
