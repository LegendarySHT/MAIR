#ifndef MOP_IR_IMPLEMENTATION_ACTIVE_MOP_ANALYSIS_ADAPTER_H
#define MOP_IR_IMPLEMENTATION_ACTIVE_MOP_ANALYSIS_ADAPTER_H

/**
 * ActiveMopAnalysis 适配器
 * 
 * 这是一个独立的实现，不依赖原项目的源代码
 * 提供与原项目 ActiveMopAnalysis 相同的接口，但实现是独立的
 */

#ifdef MOP_IR_USE_LLVM

#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include <vector>
#include <memory>

namespace MopIRImpl {
namespace MOP {

/**
 * 独立的 ActiveMopAnalysis 实现
 * 
 * 功能：检查两个 MOP 之间是否有函数调用干扰
 * 这是一个简化但独立的实现
 */
class ActiveMopAnalysis {
private:
  llvm::Function& F;
  llvm::SmallVector<const llvm::Instruction*, 64> MopList;
  llvm::DenseMap<const llvm::Instruction*, unsigned> MopMap;
  bool Analyzed;
  bool UsedForTsan;
  
  // 数据流分析结果（简化版）
  struct BlockInfo {
    llvm::BitVector Reachable;  // 哪些 MOP 可达
    llvm::BitVector NotActive;  // 哪些 MOP 不活跃（被 kill）
  };
  llvm::DenseMap<const llvm::BasicBlock*, BlockInfo> BlockInfoMap;
  
  // 检查指令是否是 clobbering call
  static bool isClobberingCall(const llvm::Instruction* Inst);
  
  // 初始化 MOP 映射
  void initializeMopMaps();
  
  // 初始化基本块信息
  void initializeBlockInfo();
  
  // 更新基本块信息
  bool updateInfo(const llvm::BasicBlock* BB);
  
  // 数据流分析
  void dataflowAnalyze();
  
  // 获取 MOP ID
  unsigned getMopId(const llvm::Instruction* I) const;
  
  // 检查 MOP 在基本块后是否活跃
  bool isMopActiveAfter(unsigned MopId, const llvm::BasicBlock* BB) const;
  
  // 检查 MOP 在基本块前是否活跃
  bool isMopActiveBefore(unsigned MopId, const llvm::BasicBlock* BB) const;
  
  // 检查同一基本块内的活跃性
  static bool isActiveInTheSameBlock(const llvm::Instruction* From,
                                     const llvm::Instruction* To);
  
public:
  /**
   * 构造函数
   * 
   * @param Func 要分析的函数
   * @param MOPs MOP 指令列表
   * @param IsTsan 是否用于 TSan（影响内存屏障处理）
   */
  ActiveMopAnalysis(llvm::Function& Func,
                   const llvm::SmallVectorImpl<const llvm::Instruction*>& MOPs,
                   bool IsTsan = false);
  
  /**
   * 检查 From 到 To 是否活跃（无函数调用干扰）
   * 
   * @param From 起始指令
   * @param To 目标指令
   * @param IsToDead 是否 To 是 dead MOP
   * @return true 如果活跃（无干扰），false 如果有干扰
   */
  bool isOneMopActiveToAnother(const llvm::Instruction* From,
                               const llvm::Instruction* To,
                               bool IsToDead) const;
};

} // namespace MOP
} // namespace MopIRImpl

#endif // MOP_IR_USE_LLVM

#endif // MOP_IR_IMPLEMENTATION_ACTIVE_MOP_ANALYSIS_ADAPTER_H

