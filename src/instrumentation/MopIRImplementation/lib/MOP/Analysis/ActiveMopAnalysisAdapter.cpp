#include "MOP/Analysis/ActiveMopAnalysisAdapter.h"

#ifdef MOP_IR_USE_LLVM

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Support/ErrorHandling.h"
#include <algorithm>

namespace MopIRImpl {
namespace MOP {

// 检查指令是否是 clobbering call
bool ActiveMopAnalysis::isClobberingCall(const llvm::Instruction* Inst) {
  if (!llvm::isa<llvm::CallBase>(Inst)) {
    return false;
  }
  
  if (llvm::isa<llvm::DbgInfoIntrinsic>(Inst)) {
    return false;
  }
  
  // 检查是否有 nosanitize 元数据
  if (Inst->hasMetadata(llvm::LLVMContext::MD_nosanitize)) {
    return false;
  }
  
  // 忽略插桩函数调用（__asan_*, __tsan_*, __msan_* 等）
  // 这些函数只是检查内存访问，不会影响实际的内存操作
  if (const llvm::CallBase* CB = llvm::dyn_cast<llvm::CallBase>(Inst)) {
    if (const llvm::Function* Callee = CB->getCalledFunction()) {
      llvm::StringRef Name = Callee->getName();
      if (Name.startswith("__asan_") || 
          Name.startswith("__tsan_") || 
          Name.startswith("__msan_") ||
          Name.startswith("__ubsan_")) {
        return false;  // 忽略插桩函数调用
      }
    }
  }
  
  // 检查是否可能写内存
  if (!Inst->mayWriteToMemory()) {
    return false;
  }
  
  return true;
}

void ActiveMopAnalysis::initializeMopMaps() {
  MopMap.clear();
  
  unsigned Id = 0;
  for (const llvm::Instruction* I : MopList) {
    MopMap[I] = Id;
    Id++;
  }
}

unsigned ActiveMopAnalysis::getMopId(const llvm::Instruction* I) const {
  auto it = MopMap.find(I);
  if (it == MopMap.end()) {
    llvm::report_fatal_error("Instruction not found in MopMap");
  }
  return it->second;
}

bool ActiveMopAnalysis::isActiveInTheSameBlock(const llvm::Instruction* From,
                                                const llvm::Instruction* To) {
  const llvm::Instruction* Inst = From;
  do {
    if (Inst == To) {
      return true;
    }
    if (isClobberingCall(Inst)) {
      return false;
    }
  } while ((Inst = Inst->getNextNonDebugInstruction()));
  
  llvm_unreachable("From and To should be in the same basic block");
}

void ActiveMopAnalysis::initializeBlockInfo() {
  unsigned NumMops = MopList.size();
  
  for (const llvm::BasicBlock& BB : F) {
    BlockInfo Info;
    Info.Reachable.resize(NumMops, false);
    Info.NotActive.resize(NumMops, false);
    
    // 检查基本块中是否有调用
    bool HasCall = false;
    for (const llvm::Instruction& I : BB) {
      if (isClobberingCall(&I)) {
        HasCall = true;
        break;
      }
    }
    
    // 如果基本块有调用，所有 MOP 都被 kill
    if (HasCall) {
      Info.NotActive.set();  // 所有位设为 1（不活跃）
    }
    
    // 标记基本块内的 MOP 为可达
    for (const llvm::Instruction& I : BB) {
      auto it = MopMap.find(&I);
      if (it != MopMap.end()) {
        unsigned MopId = it->second;
        Info.Reachable.set(MopId);
        if (HasCall) {
          Info.NotActive.set(MopId);  // 在调用之后，不活跃
        }
      }
    }
    
    BlockInfoMap[&BB] = std::move(Info);
  }
}

bool ActiveMopAnalysis::updateInfo(const llvm::BasicBlock* BB) {
  BlockInfo& Info = BlockInfoMap[BB];
  BlockInfo OldInfo = Info;
  
  // Merge: 合并所有前驱的 OUT
  Info.Reachable.reset();
  Info.NotActive.reset();
  
  for (const llvm::BasicBlock* Pred : llvm::predecessors(BB)) {
    const BlockInfo& PredInfo = BlockInfoMap[Pred];
    Info.Reachable |= PredInfo.Reachable;
    Info.NotActive |= PredInfo.NotActive;
  }
  
  // Transfer: 应用转移函数
  // 如果基本块有调用，kill 所有活跃的 MOP
  bool HasCall = false;
  for (const llvm::Instruction& I : *BB) {
    if (isClobberingCall(&I)) {
      HasCall = true;
      break;
    }
  }
  
  if (HasCall) {
    Info.NotActive |= Info.Reachable;  // Kill: 所有可达的变为不活跃
  }
  
  // Gen: 标记基本块内的 MOP
  for (const llvm::Instruction& I : *BB) {
    auto it = MopMap.find(&I);
    if (it != MopMap.end()) {
      unsigned MopId = it->second;
      Info.Reachable.set(MopId);
      if (HasCall) {
        Info.NotActive.set(MopId);
      }
    }
  }
  
  return Info.Reachable != OldInfo.Reachable || Info.NotActive != OldInfo.NotActive;
}

void ActiveMopAnalysis::dataflowAnalyze() {
  if (F.isDeclaration() || F.empty()) {
    return;
  }
  
  initializeMopMaps();
  initializeBlockInfo();
  
  // 工作列表算法
  llvm::SmallSetVector<const llvm::BasicBlock*, 64> WorkList;
  
  // 第一轮：逆后序遍历
  llvm::ReversePostOrderTraversal<const llvm::Function*> RPOT(&F);
  for (const llvm::BasicBlock* BB : RPOT) {
    WorkList.remove(BB);
    updateInfo(BB);
    
    for (const llvm::BasicBlock* Succ : llvm::successors(BB)) {
      WorkList.insert(Succ);
    }
  }
  
  // 迭代直到不动点
  while (!WorkList.empty()) {
    const llvm::BasicBlock* BB = WorkList.pop_back_val();
    
    if (!updateInfo(BB)) {
      continue;
    }
    
    for (const llvm::BasicBlock* Succ : llvm::successors(BB)) {
      WorkList.insert(Succ);
    }
  }
  
  Analyzed = true;
}

bool ActiveMopAnalysis::isMopActiveAfter(unsigned MopId, const llvm::BasicBlock* BB) const {
  const BlockInfo& Info = BlockInfoMap.find(BB)->second;
  return Info.Reachable.test(MopId) && !Info.NotActive.test(MopId);
}

bool ActiveMopAnalysis::isMopActiveBefore(unsigned MopId, const llvm::BasicBlock* BB) const {
  // 需要合并前驱的信息（简化版：直接使用当前块的信息）
  const BlockInfo& Info = BlockInfoMap.find(BB)->second;
  return Info.Reachable.test(MopId) && !Info.NotActive.test(MopId);
}

ActiveMopAnalysis::ActiveMopAnalysis(
    llvm::Function& Func,
    const llvm::SmallVectorImpl<const llvm::Instruction*>& MOPs,
    bool IsTsan)
    : F(Func), MopList(MOPs.begin(), MOPs.end()), Analyzed(false), UsedForTsan(IsTsan) {
  
  // 如果 MOP 跨多个基本块，进行数据流分析
  bool CrossBlocks = false;
  if (!MopList.empty()) {
    const llvm::BasicBlock* FirstBB = MopList[0]->getParent();
    for (const llvm::Instruction* I : MopList) {
      if (I->getParent() != FirstBB) {
        CrossBlocks = true;
        break;
      }
    }
  }
  
  if (CrossBlocks) {
    dataflowAnalyze();
  } else {
    // 如果都在同一基本块，不需要数据流分析
    initializeMopMaps();
    Analyzed = true;
  }
}

bool ActiveMopAnalysis::isOneMopActiveToAnother(const llvm::Instruction* From,
                                                const llvm::Instruction* To,
                                                bool IsToDead) const {
  const llvm::BasicBlock* FromBB = From->getParent();
  const llvm::BasicBlock* ToBB = To->getParent();
  
  // 如果都在同一基本块
  if (FromBB == ToBB) {
    return isActiveInTheSameBlock(From, To);
  }
  
  // 如果未分析（不应该发生，但安全检查）
  if (!Analyzed) {
    return false;
  }
  
  // 跨基本块分析
  unsigned FromId = getMopId(From);
  
  // 检查 From 在 ToBB 的 OUT 中是否活跃
  if (isMopActiveAfter(FromId, ToBB)) {
    return true;
  }
  
  // 检查 From 在 ToBB 的 IN 中是否活跃
  if (!isMopActiveBefore(FromId, ToBB)) {
    return false;
  }
  
  // 检查 ToBB 入口到 To 的块内活跃性
  const llvm::Instruction* Entry = ToBB->getFirstNonPHIOrDbgOrLifetime();
  return isActiveInTheSameBlock(Entry, To);
}

} // namespace MOP
} // namespace MopIRImpl

#endif // MOP_IR_USE_LLVM

