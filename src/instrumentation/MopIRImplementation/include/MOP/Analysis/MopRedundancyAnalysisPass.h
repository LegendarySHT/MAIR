#ifndef MOP_IR_IMPLEMENTATION_MOP_REDUNDANCY_ANALYSIS_PASS_H
#define MOP_IR_IMPLEMENTATION_MOP_REDUNDANCY_ANALYSIS_PASS_H

#include "../../Pass.h"
#include "../../PassContext.h"
#include "../MOPData.h"
#include "../MOPIRUnit.h"
#include <unordered_map>
#include <iostream>

#ifdef MOP_IR_USE_LLVM
#include "llvm/IR/Function.h"
#include "LLVM/LLVMPassContext.h"
#endif

namespace MopIRImpl {
namespace MOP {

#ifdef MOP_IR_USE_LLVM
// 辅助类：检查 MOP 冗余关系（与原项目的 MOPState::isMopCheckRecurring 一致）
class MopRedundancyChecker {
private:
  llvm::Function& F;
  LLVM::LLVMPassContext& Context;
  
  // 获取分析结果
  llvm::DominatorTree& getDT() {
    return Context.getDominatorTree();
  }
  
  llvm::PostDominatorTree& getPDT() {
    return Context.getPostDominatorTree();
  }
  
  llvm::AAResults& getAA() {
    return Context.getAAResults();
  }
  
  const llvm::DataLayout& getDL() {
    return Context.getDataLayout();
  }
  
  const llvm::TargetLibraryInfo& getTLI() {
    return Context.getTargetLibraryInfo();
  }
  
public:
  MopRedundancyChecker(llvm::Function& Func, LLVM::LLVMPassContext& Ctx)
    : F(Func), Context(Ctx) {}
  
  /**
   * 检查 MOP1 是否覆盖 MOP2（与原项目的 isMopCheckRecurring 一致）
   * 
   * 条件：
   * 1. 内存范围包含：MOP1 的内存范围包含 MOP2
   * 2. 支配关系：MOP1 dom MOP2 || MOP1 pdom MOP2
   * 3. （可选）写操作条件：isWrite1 || (isWrite1 == isWrite2)
   * 
   * 注意：不检查 ActiveMopAnalysis，该检查在 distillRecurringChecks 中进行
   * 
   * 返回 FromI（Instruction*）如果覆盖，否则返回 nullptr
   * - 如果 MOP1 dom MOP2，返回 KillingI
   * - 如果 MOP1 pdom MOP2，返回 DeadI
   */
  const llvm::Instruction* isCovering(Mop* Mop1, Mop* Mop2, bool WriteSensitive = false);
  
private:
  /**
   * 检查内存范围包含关系
   * 
   * 参考原项目的实现，使用 GetPointerBaseWithConstantOffset
   */
  bool isAccessRangeContains(const llvm::Instruction* KillingI,
                            const llvm::Instruction* DeadI,
                            const llvm::MemoryLocation& KillingLoc,
                            const llvm::MemoryLocation& DeadLoc);
};
#endif

// 冗余分析结果（存储在 PassContext 中）
struct MopRedundancyAnalysis {
  // MOP1 覆盖 MOP2 的关系映射 (MOP2 -> MOP1)
  std::unordered_map<Mop*, Mop*> CoveringMap;
  
  // 检查 MOP1 是否覆盖 MOP2
  bool doesCover(Mop* Mop1, Mop* Mop2) const {
    auto it = CoveringMap.find(Mop2);
    return it != CoveringMap.end() && it->second == Mop1;
  }
  
  // 获取覆盖指定 MOP 的另一个 MOP
  Mop* getCoveringMop(Mop* Mop) const {
    auto it = CoveringMap.find(Mop);
    return it != CoveringMap.end() ? it->second : nullptr;
  }
};

/**
 * MOP 冗余关系分析 Pass
 * 
 * 这是一个分析 Pass，不修改 IR，只计算冗余关系并存入 Context
 */
class MopRedundancyAnalysisPass : public MopIRImpl::Pass {
public:
  bool optimize(MopIRImpl::IRUnit& IR, MopIRImpl::PassContext& Context) override {
    if (IR.getIRUnitKind() != MopIRImpl::IRUnit::Kind::MOP)
      return false;
    auto& mopIR = static_cast<MOPIRUnit&>(IR);
    if (!mopIR.isValid()) {
      return false;
    }

    // 计算冗余关系分析结果
    auto& analysis = Context.getOrCompute<MopRedundancyAnalysis>([this, &mopIR, &Context]() {
      MopRedundancyAnalysis result;

      auto& Mops = mopIR.getMops();
      std::cout << "[DEBUG] MopRedundancyAnalysisPass: 开始分析 " 
                << Mops.size() << " 个 MOP\n";
      std::cout << "[DEBUG] MopRedundancyAnalysisPass: 检查模式: i != j (双向)\n";
      
      // 分析所有 MOP 对，找出覆盖关系（双向检查，与原项目一致）
      size_t checkedPairs = 0;
      size_t foundCovering = 0;
      for (size_t i = 0; i < Mops.size(); ++i) {
        for (size_t j = 0; j < Mops.size(); ++j) {
          if (i == j) {
            continue;
          }
          
          checkedPairs++;
          std::cout << "[DEBUG] MopRedundancyAnalysisPass: 检查 MOP[" << i 
                    << "] 是否覆盖 MOP[" << j << "]\n";
          std::cout << "[DEBUG] MopRedundancyAnalysisPass: MOP[" << i << "] 指针: " 
                    << static_cast<const void*>(Mops[i].get()) << "\n";
          std::cout << "[DEBUG] MopRedundancyAnalysisPass: MOP[" << j << "] 指针: " 
                    << static_cast<const void*>(Mops[j].get()) << "\n";
          
          // isCovering 返回 FromI（Instruction*），如果覆盖则返回非 nullptr
          const llvm::Instruction* FromI = isCovering(Mops[i].get(), Mops[j].get(), Context);
          std::cout << "[DEBUG] MopRedundancyAnalysisPass: isCovering 返回 FromI: " 
                    << static_cast<const void*>(FromI) << "\n";
          
          if (FromI) {
            foundCovering++;
            std::cout << "[DEBUG] MopRedundancyAnalysisPass: 找到覆盖关系: MOP[" << i 
                      << "] 覆盖 MOP[" << j << "], FromI = " << static_cast<const void*>(FromI) << "\n";
            result.CoveringMap[Mops[j].get()] = Mops[i].get();
            std::cout << "[DEBUG] MopRedundancyAnalysisPass: CoveringMap[MOP[" << j 
                      << "]] = MOP[" << i << "]\n";
            std::cout << "[DEBUG] MopRedundancyAnalysisPass: CoveringMap 大小: " 
                      << result.CoveringMap.size() << "\n";
          } else {
            std::cout << "[DEBUG] MopRedundancyAnalysisPass: MOP[" << i 
                      << "] 不覆盖 MOP[" << j << "]\n";
          }
        }
      }
      std::cout << "[DEBUG] MopRedundancyAnalysisPass: 总共检查了 " << checkedPairs 
                << " 对 MOP，找到 " << foundCovering << " 个覆盖关系\n";
      std::cout << "[DEBUG] MopRedundancyAnalysisPass: 最终 CoveringMap 大小: " 
                << result.CoveringMap.size() << "\n";
      
      // 打印 CoveringMap 的内容
      std::cout << "[DEBUG] MopRedundancyAnalysisPass: CoveringMap 内容:\n";
      for (const auto& [dead, killing] : result.CoveringMap) {
        std::cout << "[DEBUG]   CoveringMap[dead=" << static_cast<const void*>(dead)
                  << "] = killing=" << static_cast<const void*>(killing) << "\n";
      }
      
      return result;
    });
    
    // 分析 Pass 不修改 IR，返回 false
    return false;
  }
  
  const char* getName() const override {
    return "MopRedundancyAnalysisPass";
  }
  
private:
  // 检查 MOP1 是否覆盖 MOP2（与原项目的 isMopCheckRecurring 一致）
  // 返回 FromI（Instruction*）如果覆盖，否则返回 nullptr
  const llvm::Instruction* isCovering(Mop* Mop1, Mop* Mop2, MopIRImpl::PassContext& Context);
};

} // namespace MOP
} // namespace MopIRImpl

#endif // MOP_IR_IMPLEMENTATION_MOP_REDUNDANCY_ANALYSIS_PASS_H

