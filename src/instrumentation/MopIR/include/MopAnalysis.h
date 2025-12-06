#ifndef XSAN_MOP_IR_ANALYSIS_H
#define XSAN_MOP_IR_ANALYSIS_H

#include "Mop.h"
#include "MopContext.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/Function.h"

namespace __xsan {
namespace MopIR {

// MOP分析器基类
class MopAnalysis {
protected:
  MopContext* Context;  // 分析器可以访问上下文信息

public:
  MopAnalysis() : Context(nullptr) {}
  virtual ~MopAnalysis() = default;
  
  // 设置上下文
  void setContext(MopContext& Ctx) { Context = &Ctx; }
  
  // 分析MOP列表
  virtual void analyze(const MopList& Mops) = 0;
  
  // 获取分析器名称（用于调试和日志）
  virtual const char* getName() const = 0;
};

// 别名分析
class MopAliasAnalysis : public MopAnalysis {
private:
  llvm::DenseMap<std::pair<const Mop*, const Mop*>, bool> AliasResults;
  
public:
  void analyze(const MopList& Mops) override;
  const char* getName() const override { return "MopAliasAnalysis"; }
  
  // 查询两个MOP是否别名
  bool isAliased(const Mop* M1, const Mop* M2) const;
  
  // 清除分析结果
  void clear() { AliasResults.clear(); }
};

// 数据流分析
class MopDataflowAnalysis : public MopAnalysis {
public:
  void analyze(const MopList& Mops) override;
  const char* getName() const override { return "MopDataflowAnalysis"; }
};

// 支配关系分析
class MopDominanceAnalysis : public MopAnalysis {
private:
  // 缓存支配关系查询结果
  llvm::DenseMap<std::pair<const Mop*, const Mop*>, bool> DominanceCache;
  llvm::DenseMap<std::pair<const Mop*, const Mop*>, bool> PostDominanceCache;
  
public:
  void analyze(const MopList& Mops) override;
  const char* getName() const override { return "MopDominanceAnalysis"; }
  
  // 检查 Mop1 是否支配 Mop2
  bool dominates(const Mop* Mop1, const Mop* Mop2) const;
  
  // 检查 Mop1 是否后支配 Mop2
  bool postDominates(const Mop* Mop1, const Mop* Mop2) const;
  
  // 检查 Mop1 是否支配或后支配 Mop2
  bool dominatesOrPostDominates(const Mop* Mop1, const Mop* Mop2) const;
  
  // 清除分析结果
  void clear() {
    DominanceCache.clear();
    PostDominanceCache.clear();
  }
};

// 访问模式分析器：识别和记录内存访问模式
// 支持三种模式：
// 1. Range Access: [L, R), 步长等于访问大小
// 2. Periodic Access: ([L, R), AccessSize, Step), 按步长分割范围
// 3. Cyclic Access: ([L, R), Beg, End), 访问 [Beg, R) 和 [L, End)
class MopAccessPatternAnalysis : public MopAnalysis {
private:
  // 分析统计
  unsigned NumRangeAccesses;
  unsigned NumPeriodicAccesses;
  unsigned NumCyclicAccesses;
  unsigned NumSimpleAccesses;
  unsigned NumUnknownAccesses;
  
public:
  MopAccessPatternAnalysis()
    : NumRangeAccesses(0), NumPeriodicAccesses(0),
      NumCyclicAccesses(0), NumSimpleAccesses(0), NumUnknownAccesses(0) {}
  
  void analyze(const MopList& Mops) override;
  const char* getName() const override { return "MopAccessPatternAnalysis"; }
  
  // 获取统计信息
  unsigned getNumRangeAccesses() const { return NumRangeAccesses; }
  unsigned getNumPeriodicAccesses() const { return NumPeriodicAccesses; }
  unsigned getNumCyclicAccesses() const { return NumCyclicAccesses; }
  unsigned getNumSimpleAccesses() const { return NumSimpleAccesses; }
  unsigned getNumUnknownAccesses() const { return NumUnknownAccesses; }
  
  // 清除分析结果
  void clear() {
    NumRangeAccesses = 0;
    NumPeriodicAccesses = 0;
    NumCyclicAccesses = 0;
    NumSimpleAccesses = 0;
    NumUnknownAccesses = 0;
  }

private:
  // 分析单个 MOP 的访问模式
  bool analyzeMopAccessPattern(Mop* M);
  
  // 尝试识别范围访问模式
  bool tryIdentifyRangeAccess(Mop* M, llvm::ScalarEvolution& SE);
  
  // 尝试识别周期性访问模式
  bool tryIdentifyPeriodicAccess(Mop* M, llvm::ScalarEvolution& SE);
  
  // 尝试识别循环访问模式
  bool tryIdentifyCyclicAccess(Mop* M, llvm::ScalarEvolution& SE);
  
  // 检查循环是否为简单循环（可用于模式分析）
  bool isSimpleLoop(llvm::Loop* L) const;
  
  // 从 SCEV 提取访问范围信息
  bool extractAccessRangeFromSCEV(
      const llvm::SCEVAddRecExpr* AR,
      llvm::ScalarEvolution& SE,
      llvm::Loop* L,
      llvm::Value*& Begin,
      llvm::Value*& End,
      llvm::Value*& Step,
      bool& StepIsConstant,
      uint64_t& StepConstant) const;
  
  // 计算 MOP 的访问大小
  uint64_t getMopAccessSize(const Mop* M) const;
};

// 冗余MOP分析（用于赘余检查消除）
class MopRedundancyAnalysis : public MopAnalysis {
private:
  // 存储冗余MOP的集合
  llvm::DenseSet<const Mop*> RedundantMops;
  // 存储MOP之间的覆盖关系 (Dead -> Killing)
  llvm::DenseMap<const Mop*, const Mop*> CoveringMopMap;
  
public:
  void analyze(const MopList& Mops) override;
  const char* getName() const override { return "MopRedundancyAnalysis"; }
  
  // 判断一个MOP是否是冗余的（即是否被其他MOP的检查覆盖）
  bool isRedundant(const Mop* M) const;
  
  // 获取覆盖指定MOP的另一个MOP
  const Mop* getCoveringMop(const Mop* M) const;
  
  // 获取所有可以被消除的冗余MOP
  const llvm::DenseSet<const Mop*>& getRedundantMops() const {
    return RedundantMops;
  }
  
  // 判断MOP1是否覆盖MOP2
  bool doesMopCover(const Mop* Mop1, const Mop* Mop2) const;
  
  // 清除分析结果
  void clear() {
    RedundantMops.clear();
    CoveringMopMap.clear();
  }

private:
  // 检查两个内存操作之间是否有干扰调用
  bool hasInterferingCallBetween(const Mop* Earlier, const Mop* Later) const;
  
  // 检查MOP1的内存范围是否包含MOP2的内存范围（考虑别名）
  bool isAccessRangeContains(const Mop* Mop1, const Mop* Mop2,
                              int64_t& Off1, int64_t& Off2) const;
  
  // 检查MOP1是否支配MOP2或者后支配MOP2
  bool doesDominateOrPostDominate(const Mop* Mop1, const Mop* Mop2) const;
};

// 分析流水线（类似于优化流水线）
class MopAnalysisPipeline {
private:
  llvm::SmallVector<std::unique_ptr<MopAnalysis>, 8> Analyses;
  MopContext* Context;
  
public:
  MopAnalysisPipeline() : Context(nullptr) {}
  
  // 设置上下文
  void setContext(MopContext& Ctx) {
    Context = &Ctx;
    for (auto& Analysis : Analyses) {
      Analysis->setContext(Ctx);
    }
  }
  
  // 添加分析器到流水线
  void addAnalysis(std::unique_ptr<MopAnalysis> Analysis) {
    if (Context) {
      Analysis->setContext(*Context);
    }
    Analyses.push_back(std::move(Analysis));
  }
  
  // 运行整个分析流水线
  void run(const MopList& Mops);
  
  // 获取分析器数量
  size_t size() const { return Analyses.size(); }
  
  // 清空分析器
  void clear() { Analyses.clear(); }
};

} // namespace MopIR
} // namespace __xsan

#endif // XSAN_MOP_IR_ANALYSIS_H