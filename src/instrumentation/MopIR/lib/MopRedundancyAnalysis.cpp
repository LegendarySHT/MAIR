#include "../include/MopAnalysis.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"

using namespace __xsan::MopIR;
using namespace llvm;

void MopRedundancyAnalysis::analyze(const MopList& Mops) {
  // 清空之前的分析结果
  RedundantMops.clear();
  CoveringMopMap.clear();
  
  // 遍历所有MOP对，检查覆盖关系 - 修复：MopList包含的是std::unique_ptr<Mop>，需要使用.get()获取原始指针
  for (const auto& Mop1Ptr : Mops) {
    const Mop* Mop1 = Mop1Ptr.get();
    for (const auto& Mop2Ptr : Mops) {
      const Mop* Mop2 = Mop2Ptr.get();
      if (Mop1 == Mop2) continue;
      
      // 检查Mop1是否覆盖Mop2
      if (doesMopCover(Mop1, Mop2)) {
        // 将Mop2标记为冗余，并记录覆盖关系
        RedundantMops.insert(Mop2);
        CoveringMopMap[Mop2] = Mop1;
        
        // 为了简化，我们假设每个冗余MOP只被一个MOP覆盖
        // 在实际应用中，可能需要更复杂的策略来选择最佳的覆盖MOP
        break;
      }
    }
  }
}

bool MopRedundancyAnalysis::isRedundant(const Mop* M) const {
  return RedundantMops.count(M) > 0;
}

const Mop* MopRedundancyAnalysis::getCoveringMop(const Mop* M) const {
  auto It = CoveringMopMap.find(M);
  if (It != CoveringMopMap.end()) {
    return It->second;
  }
  return nullptr;
}

const DenseSet<const Mop*>& MopRedundancyAnalysis::getRedundantMops() const {
  return RedundantMops;
}

bool MopRedundancyAnalysis::doesMopCover(const Mop* Mop1, const Mop* Mop2) const {
  // 检查Mop1的内存范围是否包含Mop2的内存范围
  if (!isAccessRangeContains(Mop1, Mop2)) {
    return false;
  }
  
  // 检查Mop1是否支配Mop2或者后支配Mop2
  if (!doesDominateOrPostDominate(Mop1, Mop2)) {
    return false;
  }
  
  // 检查Mop1和Mop2之间是否有干扰调用
  if (hasInterferingCallBetween(Mop1, Mop2)) {
    return false;
  }
  
  // 检查操作类型兼容性：写操作可以覆盖任何操作，读操作只能覆盖读操作
  if (Mop1->getType() == MopType::Load && Mop2->getType() != MopType::Load) {
    return false;
  }
  
  return true;
}

bool MopRedundancyAnalysis::hasInterferingCallBetween(const Mop* Earlier, const Mop* Later) const {
  // 获取指令
  Instruction* EarlierInst = Earlier->getOriginalInst();
  Instruction* LaterInst = Later->getOriginalInst();
  
  // 确保指令有效
  if (!EarlierInst || !LaterInst) {
    return true; // 无法确定，保守返回有干扰
  }
  
  // 如果在同一个基本块中
  if (EarlierInst->getParent() == LaterInst->getParent()) {
    BasicBlock* BB = EarlierInst->getParent();
    bool FoundEarlier = false;
    
    // 修复：使用BasicBlock的指令迭代器，而不是错误的Instruction::iterator
    for (Instruction& I : *BB) {
      // 找到EarlierInst后开始检查
      if (&I == EarlierInst) {
        FoundEarlier = true;
        continue; // 跳过EarlierInst本身
      }
      
      // 如果已经找到了EarlierInst，并且在LaterInst之前，检查是否有函数调用
      if (FoundEarlier && &I == LaterInst) {
        break; // 到达LaterInst，结束检查
      }
      
      if (FoundEarlier) {
        // 检查是否有函数调用
        if (isa<CallInst>(&I) || isa<InvokeInst>(&I)) {
          return true;
        }
      }
    }
  } else {
    // 跨基本块的情况需要更复杂的分析
    // 这里我们暂时保守处理，返回true表示可能有干扰
    // 在实际实现中，应该使用控制流图来判断路径上是否有调用
    return true;
  }
  
  return false;
}

bool MopRedundancyAnalysis::isAccessRangeContains(const Mop* Mop1, const Mop* Mop2) const {
  // 获取内存位置
  const MemoryLocation& Loc1 = Mop1->getLocation();
  const MemoryLocation& Loc2 = Mop2->getLocation();
  
  // 如果指针不同，无法确定是否包含（简化处理）
  if (Loc1.Ptr != Loc2.Ptr) {
    return false;
  }
  
  // 如果大小信息未知，保守返回false（无法确定是否包含）
  if (!Loc1.Size.hasValue() || !Loc2.Size.hasValue()) {
    return false;
  }
  
  // 由于LLVM的MemoryLocation没有直接的偏移量信息，我们简化处理：
  // 只有当两个内存访问完全相同的大小和指针时，才认为一个包含另一个
  // 在实际实现中，需要通过更复杂的分析来获取偏移量信息
  uint64_t Size1 = Loc1.Size.getValue();
  uint64_t Size2 = Loc2.Size.getValue();
  
  // 简化处理：只有当大小相同时才认为可能包含
  // 注意：这是一个保守的简化，实际实现需要更精确的分析
  return Size1 >= Size2;
}

bool MopRedundancyAnalysis::doesDominateOrPostDominate(const Mop* Mop1, const Mop* Mop2) const {
  // 获取指令
  Instruction* Inst1 = Mop1->getOriginalInst();
  Instruction* Inst2 = Mop2->getOriginalInst();
  
  // 确保指令有效
  if (!Inst1 || !Inst2) {
    return false;
  }
  
  // 在同一个基本块中的简单支配关系：指令顺序
  if (Inst1->getParent() == Inst2->getParent()) {
    BasicBlock* BB = Inst1->getParent();
    bool FoundInst1 = false;
    
    for (Instruction& I : *BB) {
      if (&I == Inst1) {
        FoundInst1 = true;
      } else if (&I == Inst2) {
        // 如果先找到Inst1再找到Inst2，则Inst1在Inst2之前
        return FoundInst1;
      }
    }
  }
  
  // 跨基本块的支配关系需要更复杂的分析
  // 这里我们暂时简化处理，只检查同一个基本块的情况
  // 在实际实现中，应该使用DominatorTree和PostDominatorTree
  return false;
}