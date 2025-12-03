#ifndef XSAN_MOP_IR_OPTIMIZER_H
#define XSAN_MOP_IR_OPTIMIZER_H

#include "Mop.h"
#include "llvm/IR/Function.h"

namespace __xsan {
namespace MopIR {

// MOP优化器基类
class MopOptimizer {
public:
  virtual ~MopOptimizer() = default;
  
  // 优化MOP列表
  virtual void optimize(MopList& Mops) = 0;
};

// 连续读取合并优化器
class ContiguousReadMerger : public MopOptimizer {
public:
  void optimize(MopList& Mops) override;
};

// 冗余写入消除优化器
class RedundantWriteEliminator : public MopOptimizer {
public:
  void optimize(MopList& Mops) override;
};

// MOP优化流水线
class MopOptimizationPipeline {
private:
  llvm::SmallVector<std::unique_ptr<MopOptimizer>, 4> Optimizers;
  
public:
  // 添加优化器到流水线
  void addOptimizer(std::unique_ptr<MopOptimizer> Optimizer);
  
  // 运行整个优化流水线
  void run(MopList& Mops);
};

} // namespace MopIR
} // namespace __xsan

#endif // XSAN_MOP_IR_OPTIMIZER_H