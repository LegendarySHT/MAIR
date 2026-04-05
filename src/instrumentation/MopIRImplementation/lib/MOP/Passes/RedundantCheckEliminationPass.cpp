#include "MOP/Passes/RedundantCheckEliminationPass.h"
#include "MOP/Analysis/MopRedundancyAnalysisPass.h"
#include "MOP/Analysis/RecurringGraph.h"
#include "MOP/Analysis/ActiveMopAnalysisAdapter.h"
#include "LLVM/LLVMPassContext.h"
#include "PassContext.h"

#ifdef MOP_IR_USE_LLVM
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SetVector.h"
#endif

#include <algorithm>
#include <unordered_set>
#include <iostream>

namespace MopIRImpl {
namespace MOP {

bool RedundantCheckEliminationPass::optimize(MopIRImpl::IRUnit& IR, 
                                             MopIRImpl::PassContext& Context) {
  if (IR.getIRUnitKind() != MopIRImpl::IRUnit::Kind::MOP)
    return false;
  auto& mopIR = static_cast<MOPIRUnit&>(IR);
  if (!mopIR.isValid()) {
    return false;
  }

  // 调用 distillRecurringChecks 获取支配集
  DominatingSet = distillRecurringChecks(mopIR.getMops(), Context, false, false);
  
  // 标记不在支配集中的 MOP 为冗余
  bool modified = false;
  std::unordered_set<Mop*> dominatingSetSet(DominatingSet.begin(), DominatingSet.end());
  
  for (auto& mop : mopIR.getMops()) {
    if (dominatingSetSet.find(mop.get()) == dominatingSetSet.end()) {
      mop->setRedundant(true);
      modified = true;
    } else {
      mop->setRedundant(false);
    }
  }
  
  return modified;
}

std::vector<Mop*> RedundantCheckEliminationPass::distillRecurringChecks(
    const MopList& mops,
    MopIRImpl::PassContext& Context,
    bool isTsan,
    bool ignoreCalls) {
  
  if (mops.size() < 2) {
    // 如果少于 2 个 MOP，全部保留
    std::vector<Mop*> result;
    for (const auto& mop : mops) {
      result.push_back(mop.get());
    }
    return result;
  }
  
  // 收集候选 MOP（参与冗余关系的 MOP）
  std::unordered_set<Mop*> candidatesSet;
  std::vector<RecurringGraph::Edge> edges;
  
  // 根据 isTsan 设置 WriteSensitive（与原项目一致）
  bool WriteSensitive = isTsan;
  std::cout << "[DEBUG] distillRecurringChecks: WriteSensitive = " 
            << (WriteSensitive ? "true (TSan)" : "false (ASan)") << "\n";
  
  // 遍历所有 MOP 对，构建边（与原项目的实现一致）
#ifdef MOP_IR_USE_LLVM
  if (Context.getPassContextKind() == MopIRImpl::PassContext::Kind::LLVM &&
      !mops.empty()) {
    auto& llvmContext = static_cast<LLVM::LLVMPassContext&>(Context);
    // 获取第一个 MOP 的函数（所有 MOP 应该在同一个函数中）
    const llvm::Instruction* firstInst = mops[0]->getInstruction();
    if (firstInst) {
      llvm::Function* func = const_cast<llvm::Function*>(firstInst->getFunction());
      if (func) {
        llvmContext.setCurrentFunction(*func);
        
        // 创建 MopRedundancyChecker（类似原项目的 MOPState）
        // 注意：需要包含 MopRedundancyAnalysisPass.cpp 中的 MopRedundancyChecker 类
        // 或者将 MopRedundancyChecker 移到头文件中
        // 暂时使用 MopRedundancyAnalysisPass::isCovering，但需要传递 WriteSensitive
        
        for (size_t i = 0; i < mops.size(); ++i) {
          for (size_t j = 0; j < mops.size(); ++j) {
            if (i == j) {
              continue;
            }
            
            Mop* killing = mops[i].get();
            Mop* dead = mops[j].get();
            
            std::cout << "[DEBUG] distillRecurringChecks: 检查 killing[" << i 
                      << "] -> dead[" << j << "]\n";
            std::cout << "[DEBUG] distillRecurringChecks: killing 指针: " 
                      << static_cast<const void*>(killing) << "\n";
            std::cout << "[DEBUG] distillRecurringChecks: dead 指针: " 
                      << static_cast<const void*>(dead) << "\n";
            
            // 直接使用 MopRedundancyChecker 检查覆盖关系（包括 WriteSensitive）
            // 这与原项目的 State.isMopCheckRecurring(KillingI, DeadI, WriteSensitive) 一致
            const llvm::Instruction* killingInst = killing->getInstruction();
            const llvm::Instruction* deadInst = dead->getInstruction();
            
            if (!killingInst || !deadInst) {
              continue;
            }
            
            // 创建 MopRedundancyChecker 并检查覆盖关系
            MopRedundancyChecker checker(*func, llvmContext);
            const llvm::Instruction* FromI = checker.isCovering(killing, dead, WriteSensitive);
            
            std::cout << "[DEBUG] distillRecurringChecks: checker.isCovering 返回 FromI: " 
                      << static_cast<const void*>(FromI) << "\n";
            
            if (FromI) {
              std::cout << "[DEBUG] distillRecurringChecks: 发现覆盖关系: killing[" << i 
                        << "] 覆盖 dead[" << j << "], FromI = " << static_cast<const void*>(FromI) << "\n";
              
              std::cout << "[DEBUG] distillRecurringChecks: 添加边: killing -> dead, from 已确定\n";
              edges.emplace_back(killing, dead, FromI, false);
              
              std::cout << "[DEBUG] distillRecurringChecks: 将 killing 添加到 candidatesSet: " 
                        << static_cast<const void*>(killing) << "\n";
              candidatesSet.insert(killing);
              
              std::cout << "[DEBUG] distillRecurringChecks: 将 dead 添加到 candidatesSet: " 
                        << static_cast<const void*>(dead) << "\n";
              candidatesSet.insert(dead);
              
              std::cout << "[DEBUG] distillRecurringChecks: candidatesSet 当前大小: " 
                        << candidatesSet.size() << "\n";
            } else {
              std::cout << "[DEBUG] distillRecurringChecks: killing[" << i 
                        << "] 不覆盖 dead[" << j << "]\n";
            }
          }
        }
      }
    }
  }
#endif
  
  // 分离候选 MOP 和非候选 MOP
  std::vector<Mop*> distilledMops;  // 初始化为非候选 MOP
  std::vector<Mop*> candidates;     // 候选 MOP
  
  std::cout << "[DEBUG] distillRecurringChecks: candidatesSet 大小: " << candidatesSet.size() << "\n";
  std::cout << "[DEBUG] distillRecurringChecks: 打印 candidatesSet 中的所有指针:\n";
  for (const auto& ptr : candidatesSet) {
    std::cout << "[DEBUG]   candidatesSet 中的指针: " << static_cast<const void*>(ptr) << "\n";
  }
  
  std::cout << "[DEBUG] distillRecurringChecks: 开始分离候选 MOP 和非候选 MOP\n";
  for (size_t idx = 0; idx < mops.size(); ++idx) {
    const auto& mop = mops[idx];
    Mop* mopPtr = mop.get();
    std::cout << "[DEBUG] distillRecurringChecks: 检查 mops[" << idx << "] 指针: " 
              << static_cast<const void*>(mopPtr) << "\n";
    
    auto it = candidatesSet.find(mopPtr);
    if (it == candidatesSet.end()) {
      // 不在候选集中，直接保留
      std::cout << "[DEBUG] distillRecurringChecks: MOP[" << idx 
                << "] 不在 candidatesSet 中，添加到非候选 MOP\n";
      distilledMops.push_back(mopPtr);
    } else {
      std::cout << "[DEBUG] distillRecurringChecks: MOP[" << idx 
                << "] 在 candidatesSet 中找到，添加到候选 MOP\n";
      candidates.push_back(mopPtr);
    }
  }
  
  std::cout << "[DEBUG] distillRecurringChecks: 分离后，候选 MOP 数量: " << candidates.size() 
            << ", 非候选 MOP 数量: " << distilledMops.size() << "\n";
  
  std::cout << "[DEBUG] distillRecurringChecks: 打印 candidates 中的所有指针:\n";
  for (size_t idx = 0; idx < candidates.size(); ++idx) {
    std::cout << "[DEBUG]   candidates[" << idx << "] 指针: " 
              << static_cast<const void*>(candidates[idx]) << "\n";
  }
  
  std::cout << "[DEBUG] distillRecurringChecks: 打印 distilledMops 中的所有指针:\n";
  for (size_t idx = 0; idx < distilledMops.size(); ++idx) {
    std::cout << "[DEBUG]   distilledMops[" << idx << "] 指针: " 
              << static_cast<const void*>(distilledMops[idx]) << "\n";
  }
  
  // 如果没有候选 MOP，直接返回
  if (candidates.empty()) {
    std::cout << "[DEBUG] distillRecurringChecks: 没有候选 MOP，直接返回非候选 MOP\n";
    return distilledMops;
  }
  
  std::cout << "[DEBUG] distillRecurringChecks: 候选 MOP 数量: " << candidates.size() << "\n";
  std::cout << "[DEBUG] distillRecurringChecks: 边数量: " << edges.size() << "\n";
  
  std::cout << "[DEBUG] distillRecurringChecks: 准备进行 ActiveMopAnalysis 检查\n";
  
  // 在构建边之后进行 ActiveMopAnalysis 检查（与原项目一致）
  if (!ignoreCalls && !candidates.empty() && !edges.empty()) {
#ifdef MOP_IR_USE_LLVM
    if (Context.getPassContextKind() == MopIRImpl::PassContext::Kind::LLVM &&
        !mops.empty()) {
      auto& llvmContext = static_cast<LLVM::LLVMPassContext&>(Context);
      const llvm::Instruction* firstInst = mops[0]->getInstruction();
      if (firstInst) {
        llvm::Function* func = const_cast<llvm::Function*>(firstInst->getFunction());
        if (func) {
          llvmContext.setCurrentFunction(*func);
          
          // 收集候选指令
          llvm::SmallVector<const llvm::Instruction*, 16> candidateInsts;
          for (Mop* mop : candidates) {
            if (const llvm::Instruction* inst = mop->getInstruction()) {
              candidateInsts.push_back(inst);
            }
          }
          
          std::cout << "[DEBUG] distillRecurringChecks: 创建 ActiveMopAnalysis，候选指令数量: " 
                    << candidateInsts.size() << "\n";
          
          // 创建 ActiveMopAnalysis
          MopIRImpl::MOP::ActiveMopAnalysis ActiveMop(*func, candidateInsts, isTsan);
          
          // 检查每条边是否有函数调用干扰
          std::cout << "[DEBUG] distillRecurringChecks: 开始检查边的阻塞状态\n";
          for (size_t idx = 0; idx < edges.size(); ++idx) {
            auto& edge = edges[idx];
            std::cout << "[DEBUG] distillRecurringChecks: 检查边[" << idx << "]\n";
            
            const bool IsToDead = (edge.From == edge.Killing->getInstruction());
            const llvm::Instruction* To = IsToDead ? edge.Dead->getInstruction() : edge.Killing->getInstruction();
            
            // 如果 From 或 To 为空，跳过
            if (!edge.From || !To) {
              std::cout << "[DEBUG] distillRecurringChecks: 边[" << idx << "] From 或 To 为空，标记为阻塞\n";
              edge.Blocked = true;
              continue;
            }
            
            std::cout << "[DEBUG] distillRecurringChecks: 边[" << idx << "] From: ";
            edge.From->print(llvm::outs());
            std::cout << "\n[DEBUG] distillRecurringChecks: 边[" << idx << "] To: ";
            To->print(llvm::outs());
            std::cout << "\n[DEBUG] distillRecurringChecks: 边[" << idx << "] IsToDead: " 
                      << (IsToDead ? "true" : "false") << "\n";
            
            // 检查是否有函数调用干扰
            bool isActive = ActiveMop.isOneMopActiveToAnother(edge.From, To, IsToDead);
            std::cout << "[DEBUG] distillRecurringChecks: 边[" << idx << "] isOneMopActiveToAnother: " 
                      << (isActive ? "true" : "false") << "\n";
            
            edge.Blocked = !isActive;
            
            std::cout << "[DEBUG] distillRecurringChecks: 边 (" 
                      << static_cast<const void*>(edge.Killing) << " -> " 
                      << static_cast<const void*>(edge.Dead) << "): "
                      << (edge.Blocked ? "阻塞" : "未阻塞") << "\n";
          }
        }
      }
    }
#endif
  }
  
  std::cout << "[DEBUG] distillRecurringChecks: 准备构建图，候选 MOP 数量: " 
            << candidates.size() << ", 边数量: " << edges.size() << "\n";
  
  if (candidates.empty()) {
    std::cout << "[DEBUG] distillRecurringChecks: 警告！candidates 为空，无法构建图\n";
  }
  
  if (edges.empty()) {
    std::cout << "[DEBUG] distillRecurringChecks: 警告！edges 为空，图将没有边\n";
  }
  
  // 构建冗余图
  std::cout << "[DEBUG] distillRecurringChecks: 调用 RecurringGraph 构造函数\n";
  RecurringGraph graph(candidates, edges);
  std::cout << "[DEBUG] distillRecurringChecks: RecurringGraph 构建完成\n";
  
  // 求解支配集
  std::cout << "[DEBUG] distillRecurringChecks: 调用 fillDominatingSet\n";
  std::vector<Mop*> dominatingSet;
  graph.fillDominatingSet(dominatingSet);
  std::cout << "[DEBUG] distillRecurringChecks: fillDominatingSet 返回，支配集大小: " 
            << dominatingSet.size() << "\n";
  
  std::cout << "[DEBUG] distillRecurringChecks: 打印支配集中的所有指针:\n";
  for (size_t idx = 0; idx < dominatingSet.size(); ++idx) {
    std::cout << "[DEBUG]   dominatingSet[" << idx << "] 指针: " 
              << static_cast<const void*>(dominatingSet[idx]) << "\n";
  }
  
  std::cout << "[DEBUG] distillRecurringChecks: 非候选 MOP 数量: " << distilledMops.size() << "\n";
  
  // 合并结果：非候选 MOP + 支配集中的候选 MOP
  std::cout << "[DEBUG] distillRecurringChecks: 合并结果（非候选 MOP + 支配集）\n";
  distilledMops.insert(distilledMops.end(), dominatingSet.begin(), dominatingSet.end());
  
  std::cout << "[DEBUG] distillRecurringChecks: 最终保留的 MOP 数量: " << distilledMops.size() << "\n";
  
  std::cout << "[DEBUG] distillRecurringChecks: 打印最终结果中的所有指针:\n";
  for (size_t idx = 0; idx < distilledMops.size(); ++idx) {
    std::cout << "[DEBUG]   distilledMops[" << idx << "] 指针: " 
              << static_cast<const void*>(distilledMops[idx]) << "\n";
  }
  
  return distilledMops;
}

} // namespace MOP
} // namespace MopIRImpl
