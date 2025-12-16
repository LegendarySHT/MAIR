#include "../include/MopOptimizer.h"
#include "../include/MopContext.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Instructions.h"
#include "../include/ActiveMopAnalysis.h"

using namespace __xsan::MopIR;
using namespace llvm;

// ============================================================================
// MopOptimizationPipeline 实现
// ============================================================================

void MopOptimizationPipeline::run(MopList& Mops) {
  if (!Context) {
    return;  // 没有上下文，无法运行
  }
  
  // 依次运行每个优化器
  for (auto &Optimizer : Optimizers) {
    if (Optimizer) {
      Optimizer->optimize(Mops);
    }
  }
}

// ============================================================================
// ContiguousReadMerger 实现（占位实现）
// ============================================================================

void ContiguousReadMerger::optimize(MopList& Mops) {
  if (!Context) {
    return;
  }
  
  // TODO: 实现连续读取合并优化
  // 这里先留空，后续可以添加：
  // 1. 识别连续的读取操作
  // 2. 检查内存位置是否相邻
  // 3. 合并相邻的读取操作
}

// ============================================================================
// RedundantWriteEliminator 实现（占位实现）
// ============================================================================

void RedundantWriteEliminator::optimize(MopList& Mops) {
  if (!Context) {
    return;
  }
  
  // TODO: 实现冗余写入消除优化
  // 这里先留空，后续可以添加：
  // 1. 识别冗余的写入操作
  // 2. 标记为冗余
  // 3. 在插桩时跳过
}

// ============================================================================
// MopRecurrenceOptimizer 实现（占位实现）
// ============================================================================

void MopRecurrenceOptimizer::optimize(MopList& Mops) {
  if (!Context) {
    return;
  }
  
  // 使用 BatchAA + SCEV 的 MOPState 进行更精准的包含判断
    MOPState State(*Context); // 为本轮计算构建状态（BatchAA/SCEV）

  // 构建赘余图，提取支配集，然后标记非支配集为冗余
  llvm::SmallVector<Mop*, 16> dominating;
  buildRecurringGraphAndFindDominatingSet(Mops, dominating);

  // 将非支配集节点标记为冗余，支配集为非冗余
  llvm::SmallPtrSet<Mop*, 32> domSet;
  for (auto *M : dominating) domSet.insert(M);
  for (auto &UP : Mops) {
    Mop *M = UP.get();
    bool isDom = domSet.contains(M);
    M->setRedundant(!isDom);
    if (!isDom) {
      // 为简化，覆盖者未知，这里不设置 CoveringMop。
      M->setCoveringMop(nullptr);
    }
  }
}

bool MopRecurrenceOptimizer::isMopCheckRecurring(Mop* KillingMop, Mop* DeadMop, bool WriteSensitive, MOPState &State) {
  if (!Context || !KillingMop || !DeadMop) {
    return false;
  }
  
  llvm::Instruction *KillingI = KillingMop->getOriginalInst();
  llvm::Instruction *DeadI = DeadMop->getOriginalInst();
  if (!KillingI || !DeadI) return false;

  // 写敏感约束：TSan 模式需要写或同类型
  if (WriteSensitive && !KillingMop->isWrite() &&
      (KillingMop->isWrite() != DeadMop->isWrite())) {
    return false;
  }

  // 支配或后支配关系
  auto &DT = Context->getDominatorTree();
  auto &PDT = Context->getPostDominatorTree();
  bool dominates = DT.dominates(KillingI, DeadI);
  bool postdominates = PDT.dominates(KillingI, DeadI);
  if (!dominates && !postdominates) return false;

  // 访问范围包含
  int64_t KillingOff = 0, DeadOff = 0;
  if (!isAccessRangeContains(KillingMop, DeadMop, KillingOff, DeadOff, State))
    return false;

  return true;
}

bool MopRecurrenceOptimizer::isAccessRangeContains(Mop* KillingMop, Mop* DeadMop,
                                                     int64_t& KillingOff, int64_t& DeadOff,
                                                     MOPState &State) {
  if (!Context || !KillingMop || !DeadMop) {
    return false;
  }

  llvm::Instruction *KillingI = KillingMop->getOriginalInst();
  llvm::Instruction *DeadI = DeadMop->getOriginalInst();
  const llvm::MemoryLocation &KillingLoc = KillingMop->getLocation();
  const llvm::MemoryLocation &DeadLoc = DeadMop->getLocation();
  return State.isAccessRangeContains(KillingI, DeadI, KillingLoc, DeadLoc,
                                     KillingOff, DeadOff);
}

void MopRecurrenceOptimizer::buildRecurringGraphAndFindDominatingSet(
    const MopList& Mops,
    SmallVectorImpl<Mop*>& DominatingSet) {
  if (!Context) {
    return;
  }
  // 构建本地 MOPState 以支持 BatchAA/SCEV
  MOPState State(*Context);
  
  // 枚举两两 MOP，构建有向边 Killing -> Dead 若 Dead 的检查被 Killing 覆盖
  struct Edge { Mop *Killing; Mop *Dead; bool Blocked; llvm::Instruction *FromI; llvm::Instruction *ToI; };
  llvm::SmallVector<Edge, 32> edges;
  llvm::SmallVector<Mop*, 32> candidates;
  llvm::SmallPtrSet<Mop*, 32> candSet;

  bool writeSensitive = IsTsan;

  auto hasInterferingCallBetween = [&](llvm::Instruction *From, llvm::Instruction *To, bool IsToDead) -> bool {
    if (IgnoreCalls) return false;
    if (!From || !To) return false;
    // 使用 ActiveMopAnalysis 进行同块/跨块的可达性与干扰判定
    // 这里不直接实例化（成本高），在后续统一调用 Analysis 判断；此处返回值暂不使用。
    return false;
  };

  for (const auto &Uk : Mops) {
    Mop *Km = Uk.get();
    for (const auto &Ud : Mops) {
      Mop *Dm = Ud.get();
      if (Km == Dm) continue;
        if (isMopCheckRecurring(Km, Dm, writeSensitive, State)) {
        // 根据支配/后支配关系确定 From/To 指令
        auto *KI = Km->getOriginalInst();
        auto *DI = Dm->getOriginalInst();
        auto &DT = Context->getDominatorTree();
        auto &PDT = Context->getPostDominatorTree();
        llvm::Instruction *FromI = nullptr;
        llvm::Instruction *ToI = nullptr;
        if (DT.dominates(KI, DI)) { FromI = KI; ToI = DI; }
        else if (PDT.dominates(KI, DI)) { FromI = DI; ToI = KI; }
        else { continue; }
        edges.push_back({Km, Dm, /*Blocked=*/false, FromI, ToI});
        candSet.insert(Km);
        candSet.insert(Dm);
      }
    }
  }

  for (auto *M : candSet) candidates.push_back(M);

  // 使用 ActiveMopAnalysis 进行调用干扰阻断（同块/跨块）
  if (!IgnoreCalls && !edges.empty()) {
    llvm::SmallVector<const llvm::Instruction*, 64> MopInsts;
    for (auto *M : candidates) MopInsts.push_back(M->getOriginalInst());
    __xsan::ActiveMopAnalysis AMA(Context->getFunction(), MopInsts, IsTsan);
    for (auto &E : edges) {
      // 判断 FromI 到 ToI 是否“活跃”，否则视为被干扰阻断
      bool isToDead = (E.FromI == E.Killing->getOriginalInst());
      bool active = AMA.isOneMopActiveToAnother(E.FromI, E.ToI, isToDead);
      E.Blocked = !active;
    }
  }

  // 构建图并按 RecurringGraph 逻辑计算支配集
  struct Vertex { Mop *M; llvm::SmallVector<Vertex*, 16> Children; bool HasParent=false; };
  llvm::SmallVector<std::unique_ptr<Vertex>, 64> verts;
  llvm::DenseMap<Mop*, Vertex*> map;
  for (auto *M : candidates) {
    verts.emplace_back(std::make_unique<Vertex>(Vertex{M,{/*empty*/},false}));
    map[M] = verts.back().get();
  }
  for (auto &E : edges) {
    if (E.Blocked) continue;
    auto *K = map.lookup(E.Killing);
    auto *D = map.lookup(E.Dead);
    if (!K || !D) continue;
    K->Children.push_back(D);
    D->HasParent = true;
  }

  DominatingSet.clear();
  llvm::SmallPtrSet<Vertex*, 32> visited;
  auto dfs = [&](auto &self, Vertex *v) -> void {
    if (!visited.insert(v).second) return;
    for (auto *c : v->Children) self(self, c);
  };
  // 1) 没有父节点者加入支配集并 DFS 标记
  for (auto &up : verts) {
    Vertex *v = up.get();
    if (!v->HasParent) {
      DominatingSet.push_back(v->M);
      dfs(dfs, v);
    }
  }
  // 2) 对仍未访问的节点，作为新入口加入支配集
  for (auto &up : verts) {
    Vertex *v = up.get();
    if (visited.contains(v)) continue;
    DominatingSet.push_back(v->M);
    dfs(dfs, v);
  }
  // 3) 未出现在候选集合的 MOP 原样保留
  for (const auto &U : Mops) {
    Mop *M = U.get();
    if (!candSet.contains(M)) DominatingSet.push_back(M);
  }
}
