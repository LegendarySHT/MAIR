#ifndef XSAN_MOP_IR_OPTIMIZER_H
#define XSAN_MOP_IR_OPTIMIZER_H

#include "Mop.h"
#include "MopContext.h"
#include "llvm/IR/Function.h"

namespace __xsan {
namespace MopIR {

// MOP优化器基类
class MopOptimizer {
protected:
  MopContext* Context;  // 优化器可以访问上下文信息

public:
  MopOptimizer() : Context(nullptr) {}
  virtual ~MopOptimizer() = default;
  
  // 设置上下文（由优化流水线调用）
  void setContext(MopContext& Ctx) { Context = &Ctx; }
  
  // 优化MOP列表
  virtual void optimize(MopList& Mops) = 0;
  
  // 获取优化器名称（用于调试和日志）
  virtual const char* getName() const = 0;
};

// 连续读取合并优化器
class ContiguousReadMerger : public MopOptimizer {
public:
  void optimize(MopList& Mops) override;
  const char* getName() const override { return "ContiguousReadMerger"; }
};

// 冗余写入消除优化器
class RedundantWriteEliminator : public MopOptimizer {
public:
  void optimize(MopList& Mops) override;
  const char* getName() const override { return "RedundantWriteEliminator"; }
};

// 赘余检查消除优化器（整合自 MopRecurrenceReducer）
// 这是核心优化器，用于消除被其他MOP检查覆盖的冗余检查
class MopRecurrenceOptimizer : public MopOptimizer {
private:
  bool IsTsan;           // 是否为 TSan 模式（写敏感）
  bool IgnoreCalls;      // 是否忽略调用检查（用于测试）

public:
  MopRecurrenceOptimizer(bool Tsan = false, bool IgnoreCallsCheck = false)
      : IsTsan(Tsan), IgnoreCalls(IgnoreCallsCheck) {}

  void optimize(MopList& Mops) override;
  const char* getName() const override { return "MopRecurrenceOptimizer"; }
  
  // 设置优化选项
  void setTsanMode(bool Tsan) { IsTsan = Tsan; }
  void setIgnoreCalls(bool Ignore) { IgnoreCalls = Ignore; }

private:
  // 检查两个MOP之间是否存在赘余关系
  bool isMopCheckRecurring(Mop* KillingMop, Mop* DeadMop, bool WriteSensitive);
  
  // 检查内存访问范围是否包含（考虑别名）
  bool isAccessRangeContains(Mop* KillingMop, Mop* DeadMop,
                             int64_t& KillingOff, int64_t& DeadOff);

  // 构建赘余图并找到支配集
  void buildRecurringGraphAndFindDominatingSet(
      const MopList& Mops,
      llvm::SmallVectorImpl<Mop*>& DominatingSet);
};

// MOP优化流水线
class MopOptimizationPipeline {
private:
  llvm::SmallVector<std::unique_ptr<MopOptimizer>, 8> Optimizers;
  MopContext* Context;
  
public:
  MopOptimizationPipeline() : Context(nullptr) {}
  
  // 设置上下文（所有优化器共享）
  void setContext(MopContext& Ctx) {
    Context = &Ctx;
    for (auto& Opt : Optimizers) {
      Opt->setContext(Ctx);
    }
  }
  
  // 添加优化器到流水线
  void addOptimizer(std::unique_ptr<MopOptimizer> Optimizer) {
    if (Context) {
      Optimizer->setContext(*Context);
    }
    Optimizers.push_back(std::move(Optimizer));
  }
  
  // 运行整个优化流水线
  void run(MopList& Mops);
  
  // 获取优化器数量
  size_t size() const { return Optimizers.size(); }
  
  // 清空优化器
  void clear() { Optimizers.clear(); }
};

} // namespace MopIR
} // namespace __xsan

#endif // XSAN_MOP_IR_OPTIMIZER_H