#include "../include/MopAnalysis.h"
#include "../include/MopContext.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/raw_ostream.h"

using namespace __xsan::MopIR;
using namespace llvm;

// ============================================================================
// MopAnalysisPipeline 实现
// ============================================================================

void MopAnalysisPipeline::run(const MopList& Mops) {
  if (!Context) {
    return;  // 没有上下文，无法运行
  }
  
  // 依次运行每个分析器
  for (auto& Analysis : Analyses) {
    if (Analysis) {
      Analysis->analyze(Mops);
    }
  }
}

// ============================================================================
// MopAliasAnalysis 实现（占位实现）
// ============================================================================

void MopAliasAnalysis::analyze(const MopList& Mops) {
  if (!Context) {
    return;
  }
  
  // TODO: 实现别名分析逻辑
  // 这里先清空之前的结果
  AliasResults.clear();
  
  // 遍历所有 MOP 对，进行别名分析
  for (size_t i = 0; i < Mops.size(); ++i) {
    for (size_t j = i + 1; j < Mops.size(); ++j) {
      Mop* M1 = Mops[i].get();
      Mop* M2 = Mops[j].get();
      
      if (!M1 || !M2) continue;
      
      // 使用 AliasAnalysis 进行查询
      auto& AA = Context->getAAResults();
      AliasResult AR = AA.alias(M1->getLocation(), M2->getLocation());
      
      // 存储结果（NoAlias = false, 其他 = true）
      bool IsAliased = (AR != AliasResult::NoAlias);
      AliasResults[{M1, M2}] = IsAliased;
      AliasResults[{M2, M1}] = IsAliased;  // 对称关系
    }
  }
}

bool MopAliasAnalysis::isAliased(const Mop* M1, const Mop* M2) const {
  auto It = AliasResults.find({M1, M2});
  if (It != AliasResults.end()) {
    return It->second;
  }
  return false;  // 默认假设不别名
}

// ============================================================================
// MopDataflowAnalysis 实现（占位实现）
// ============================================================================

void MopDataflowAnalysis::analyze(const MopList& Mops) {
  if (!Context) {
    return;
  }
  
  // TODO: 实现数据流分析逻辑
  // 这里先留空，后续可以添加数据流分析
}

// ============================================================================
// MopDominanceAnalysis 实现
// ============================================================================

void MopDominanceAnalysis::analyze(const MopList& Mops) {
  if (!Context) {
    return;
  }
  
  // 清空之前的结果
  DominanceCache.clear();
  PostDominanceCache.clear();
  
  auto& DT = Context->getDominatorTree();
  auto& PDT = Context->getPostDominatorTree();
  
  // 遍历所有 MOP 对，进行支配关系分析
  for (size_t i = 0; i < Mops.size(); ++i) {
    for (size_t j = 0; j < Mops.size(); ++j) {
      if (i == j) continue;
      
      Mop* M1 = Mops[i].get();
      Mop* M2 = Mops[j].get();
      
      if (!M1 || !M2 || !M1->getOriginalInst() || !M2->getOriginalInst()) {
        continue;
      }
      
      Instruction* I1 = M1->getOriginalInst();
      Instruction* I2 = M2->getOriginalInst();
      
      // 检查支配关系
      bool Dom = DT.dominates(I1, I2);
      DominanceCache[{M1, M2}] = Dom;
      
      // 检查后支配关系
      bool PostDom = PDT.dominates(I1, I2);
      PostDominanceCache[{M1, M2}] = PostDom;
    }
  }
}

bool MopDominanceAnalysis::dominates(const Mop* Mop1, const Mop* Mop2) const {
  auto It = DominanceCache.find({Mop1, Mop2});
  if (It != DominanceCache.end()) {
    return It->second;
  }
  return false;
}

bool MopDominanceAnalysis::postDominates(const Mop* Mop1, const Mop* Mop2) const {
  auto It = PostDominanceCache.find({Mop1, Mop2});
  if (It != PostDominanceCache.end()) {
    return It->second;
  }
  return false;
}

bool MopDominanceAnalysis::dominatesOrPostDominates(const Mop* Mop1, const Mop* Mop2) const {
  return dominates(Mop1, Mop2) || postDominates(Mop1, Mop2);
}

// ============================================================================
// MopRedundancyAnalysis 实现（占位实现）
// ============================================================================

void MopRedundancyAnalysis::analyze(const MopList& Mops) {
  if (!Context) {
    return;
  }
  
  // 清空之前的结果
  RedundantMops.clear();
  CoveringMopMap.clear();
  
  // TODO: 实现完整的冗余分析逻辑
  // 这里先留空，后续需要实现：
  // 1. 检查内存范围包含关系
  // 2. 检查支配关系
  // 3. 检查干扰调用
  // 4. 标记冗余 MOP
}

bool MopRedundancyAnalysis::isRedundant(const Mop* M) const {
  return RedundantMops.contains(M);
}

const Mop* MopRedundancyAnalysis::getCoveringMop(const Mop* M) const {
  auto It = CoveringMopMap.find(M);
  if (It != CoveringMopMap.end()) {
    return It->second;
  }
  return nullptr;
}

bool MopRedundancyAnalysis::doesMopCover(const Mop* Mop1, const Mop* Mop2) const {
  // TODO: 实现覆盖检查逻辑
  return false;
}

bool MopRedundancyAnalysis::hasInterferingCallBetween(const Mop* Earlier, const Mop* Later) const {
  // TODO: 实现干扰调用检查
  return false;
}

bool MopRedundancyAnalysis::isAccessRangeContains(const Mop* Mop1, const Mop* Mop2,
                                                    int64_t& Off1, int64_t& Off2) const {
  // TODO: 实现访问范围包含检查
  return false;
}

bool MopRedundancyAnalysis::doesDominateOrPostDominate(const Mop* Mop1, const Mop* Mop2) const {
  if (!Context) {
    return false;
  }
  
  // 使用支配关系分析器
  MopDominanceAnalysis DomAnalysis;
  DomAnalysis.setContext(*Context);
  return DomAnalysis.dominatesOrPostDominates(Mop1, Mop2);
}
