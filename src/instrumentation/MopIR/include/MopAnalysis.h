#ifndef XSAN_MOP_IR_ANALYSIS_H
#define XSAN_MOP_IR_ANALYSIS_H

#include "Mop.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/Function.h"

namespace __xsan {
namespace MopIR {

// MOP分析器基类
class MopAnalysis {
public:
  virtual ~MopAnalysis() = default;
  
  // 分析MOP列表
  virtual void analyze(const MopList& Mops) = 0;
};

// 别名分析
class MopAliasAnalysis : public MopAnalysis {
private:
  llvm::DenseMap<std::pair<Mop*, Mop*>, bool> AliasResults;
  
public:
  void analyze(const MopList& Mops) override;
  
  // 查询两个MOP是否别名
  bool isAliased(const Mop* M1, const Mop* M2);
};

// 数据流分析
class MopDataflowAnalysis : public MopAnalysis {
public:
  void analyze(const MopList& Mops) override;
};

// 冗余MOP分析
class MopRedundancyAnalysis : public MopAnalysis {
private:
  // 存储冗余MOP的集合
  llvm::DenseSet<const Mop*> RedundantMops;
  // 存储MOP之间的覆盖关系
  llvm::DenseMap<const Mop*, const Mop*> CoveringMopMap;
  
public:
  void analyze(const MopList& Mops) override;
  
  // 判断一个MOP是否是冗余的（即是否被其他MOP的检查覆盖）
  bool isRedundant(const Mop* M) const;
  
  // 获取覆盖指定MOP的另一个MOP
  const Mop* getCoveringMop(const Mop* M) const;
  
  // 获取所有可以被消除的冗余MOP
  const llvm::DenseSet<const Mop*>& getRedundantMops() const;
  
  // 判断MOP1是否覆盖MOP2
  bool doesMopCover(const Mop* Mop1, const Mop* Mop2) const;
  
private:
  // 检查两个内存操作之间是否有干扰调用
  bool hasInterferingCallBetween(const Mop* Earlier, const Mop* Later) const;
  
  // 检查MOP1的内存范围是否包含MOP2的内存范围（考虑别名）
  bool isAccessRangeContains(const Mop* Mop1, const Mop* Mop2) const;
  
  // 检查MOP1是否支配MOP2或者后支配MOP2
  bool doesDominateOrPostDominate(const Mop* Mop1, const Mop* Mop2) const;
};

} // namespace MopIR
} // namespace __xsan

#endif // XSAN_MOP_IR_ANALYSIS_H