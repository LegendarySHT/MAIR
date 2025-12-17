#include <iterator>
#include <optional>
#include <optional>
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"

#include "../include/MopAnalysis.h"
#include "../include/MopContext.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Operator.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Support/TypeSize.h"
#include "llvm/Support/raw_ostream.h"
#include "../include/MOPState.h"
#include "../include/ActiveMopAnalysis.h"

using namespace __xsan::MopIR;
using namespace llvm;


namespace {
// Mirror helper from MopRecurrenceReducer: derive precise object size if known.
std::optional<TypeSize> getPointerSize(const Value *V, const DataLayout &DL,
                                       const TargetLibraryInfo &TLI,
                                       const Function *F) {
  uint64_t Size;
  ObjectSizeOpts Opts;
  Opts.NullIsUnknownSize = NullPointerIsDefined(F);

  if (getObjectSize(V, Size, DL, &TLI, Opts)) {
    return TypeSize::getFixed(Size);
  }
  return std::nullopt;
}
} // namespace

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

  // 复用原优化器的复发图逻辑：构建覆盖图，提取支配（幸存）集合
  llvm::SmallVector<Mop*, 16> Dominating;
  llvm::DenseMap<Mop*, Mop*> Covering;
  buildRecurringGraphAndFindDominatingSet(Mops, Dominating, Covering);

  llvm::SmallPtrSet<Mop*, 32> DomSet;
  for (auto *M : Dominating) DomSet.insert(M);

  for (const auto &UP : Mops) {
    Mop *M = UP.get();
    bool IsDom = DomSet.contains(M);
    M->setRedundant(!IsDom);
    if (IsDom) {
      M->setCoveringMop(nullptr);
      continue;
    }
    auto It = Covering.find(M);
    const Mop *Cover = (It != Covering.end()) ? It->second : nullptr;
    if (Cover) {
      CoveringMopMap[M] = Cover;
    }
    RedundantMops.insert(M);
    M->setCoveringMop(const_cast<Mop*>(Cover));
  }
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

bool MopRedundancyAnalysis::isMopCheckRecurring(Mop* KillingMop, Mop* DeadMop,
                                                bool WriteSensitive,
                                                MOPState &State) {
  if (!Context || !KillingMop || !DeadMop) {
    return false;
  }

  Instruction *KillingI = KillingMop->getOriginalInst();
  Instruction *DeadI = DeadMop->getOriginalInst();
  if (!KillingI || !DeadI) {
    return false;
  }

  // 写敏感约束：TSan 模式需要写或类型一致
  if (WriteSensitive && !KillingMop->isWrite() &&
      (KillingMop->isWrite() != DeadMop->isWrite())) {
    return false;
  }

  // ASan 模式下仍禁止“读覆盖写”，以与 legacy Reducer 行为对齐
  if (!WriteSensitive && !KillingMop->isWrite() && DeadMop->isWrite()) {
    return false;
  }

  auto &DT = Context->getDominatorTree();
  auto &PDT = Context->getPostDominatorTree();
  bool Dominates = DT.dominates(KillingI, DeadI);
  bool PostDominates = PDT.dominates(KillingI, DeadI);
  if (!Dominates && !PostDominates) {
    return false;
  }

  int64_t KillingOff = 0;
  int64_t DeadOff = 0;
  if (!isAccessRangeContains(KillingMop, DeadMop, KillingOff, DeadOff, State)) {
    return false;
  }

  return true;
}

bool MopRedundancyAnalysis::isAccessRangeContains(Mop* KillingMop, Mop* DeadMop,
                                                  int64_t &KillingOff,
                                                  int64_t &DeadOff,
                                                  MOPState &State) {
  if (!Context || !KillingMop || !DeadMop) {
    return false;
  }

  Instruction *KillingI = KillingMop->getOriginalInst();
  Instruction *DeadI = DeadMop->getOriginalInst();
  const MemoryLocation &KillingLoc = KillingMop->getLocation();
  const MemoryLocation &DeadLoc = DeadMop->getLocation();
  return State.isAccessRangeContains(KillingI, DeadI, KillingLoc, DeadLoc,
                                     KillingOff, DeadOff);
}

void MopRedundancyAnalysis::buildRecurringGraphAndFindDominatingSet(
    const MopList &Mops,
    SmallVectorImpl<Mop *> &DominatingSet,
    llvm::DenseMap<Mop *, Mop *> &CoveringMap) {
  if (!Context) {
    return;
  }

  MOPState State(*Context);
  struct Edge {
    Mop *Killing;
    Mop *Dead;
    bool Blocked;
    Instruction *FromI;
    Instruction *ToI;
  };

  SmallVector<Edge, 32> Edges;
  SmallVector<Mop *, 32> Candidates;
  SmallPtrSet<Mop *, 32> CandSet;
  bool WriteSensitive = IsTsan;

  for (const auto &Uk : Mops) {
    Mop *Km = Uk.get();
    Instruction *KI = Km ? Km->getOriginalInst() : nullptr;
    if (!KI || !__xsan::isInterestingMop(*KI)) {
      continue;  // 与 MopRecurrenceReducer 保持一致：仅处理“interesting” MOP
    }
    for (const auto &Ud : Mops) {
      Mop *Dm = Ud.get();
      if (Km == Dm) {
        continue;
      }
      Instruction *DI = Dm ? Dm->getOriginalInst() : nullptr;
      if (!DI || !__xsan::isInterestingMop(*DI)) {
        continue;
      }
      if (!isMopCheckRecurring(Km, Dm, WriteSensitive, State)) {
        continue;
      }
      auto &DT = Context->getDominatorTree();
      auto &PDT = Context->getPostDominatorTree();
      Instruction *FromI = nullptr;
      Instruction *ToI = nullptr;
      if (DT.dominates(KI, DI)) {
        FromI = KI;
        ToI = DI;
      } else if (PDT.dominates(KI, DI)) {
        FromI = DI;
        ToI = KI;
      } else {
        continue;
      }
      Edges.push_back({Km, Dm, /*Blocked=*/false, FromI, ToI});
      // Debug: 边构建日志
      llvm::outs() << "[MopIR Redund] EdgeAdded in "
                   << Context->getFunction().getName() << ":\n  Killing: ";
      KI->print(llvm::outs());
      llvm::outs() << "\n  Dead   : ";
      DI->print(llvm::outs());
      llvm::outs() << "\n  From -> To: ";
      FromI->print(llvm::outs());
      llvm::outs() << " -> ";
      ToI->print(llvm::outs());
      llvm::outs() << "\n";
      CandSet.insert(Km);
      CandSet.insert(Dm);
    }
  }

  for (auto *M : CandSet) {
    Candidates.push_back(M);
  }

  if (!IgnoreCalls && !Edges.empty()) {
    SmallVector<const Instruction *, 64> MopInsts;
    for (auto *M : Candidates) {
      MopInsts.push_back(M->getOriginalInst());
    }
    __xsan::ActiveMopAnalysis AMA(Context->getFunction(), MopInsts, IsTsan);
    for (auto &E : Edges) {
      bool IsToDead = (E.FromI == E.Killing->getOriginalInst());
      bool Active = AMA.isOneMopActiveToAnother(E.FromI, E.ToI, IsToDead);
      E.Blocked = !Active;
      // Debug: 边阻断状态
      llvm::outs() << "[MopIR Redund] EdgeActive in "
                   << Context->getFunction().getName() << ": ";
      E.FromI->print(llvm::outs());
      llvm::outs() << (Active ? " ->(active) " : " ->(blocked) ");
      E.ToI->print(llvm::outs());
      llvm::outs() << "\n";
    }
  }

  struct Vertex {
    Mop *M;
    SmallVector<Vertex *, 16> Children;
    bool HasParent = false;
  };
  SmallVector<std::unique_ptr<Vertex>, 64> Verts;
  DenseMap<Mop *, Vertex *> Map;
  for (auto *M : Candidates) {
    Verts.emplace_back(std::make_unique<Vertex>(Vertex{M, {}, false}));
    Map[M] = Verts.back().get();
  }
  for (auto &E : Edges) {
    if (E.Blocked) {
      continue;
    }
    auto *K = Map.lookup(E.Killing);
    auto *D = Map.lookup(E.Dead);
    if (!K || !D) {
      continue;
    }
    K->Children.push_back(D);
    D->HasParent = true;
    if (!CoveringMap.lookup(D->M)) {
      CoveringMap[D->M] = K->M;
    }
  }

  DominatingSet.clear();
  SmallPtrSet<Vertex *, 32> Visited;
  auto Dfs = [&](auto &Self, Vertex *V) -> void {
    if (!Visited.insert(V).second) {
      return;
    }
    for (auto *C : V->Children) {
      Self(Self, C);
    }
  };

  for (auto &UP : Verts) {
    Vertex *V = UP.get();
    if (!V->HasParent) {
      DominatingSet.push_back(V->M);
      Dfs(Dfs, V);
    }
  }

  for (auto &UP : Verts) {
    Vertex *V = UP.get();
    if (Visited.contains(V)) {
      continue;
    }
    DominatingSet.push_back(V->M);
    Dfs(Dfs, V);
  }

  for (const auto &U : Mops) {
    Mop *M = U.get();
    if (!CandSet.contains(M)) {
      DominatingSet.push_back(M);
    }
  }

  // Debug: 最终幸存集合
  llvm::outs() << "[MopIR Redund] Dominating survivors in "
               << Context->getFunction().getName() << " (" << DominatingSet.size() << ")\n";
  for (auto *M : DominatingSet) {
    if (auto *I = M ? M->getOriginalInst() : nullptr) {
      llvm::outs() << "  ";
      I->print(llvm::outs());
      llvm::outs() << "\n";
    }
  }
}
