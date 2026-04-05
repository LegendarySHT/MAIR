#ifndef MOP_IR_IMPLEMENTATION_REDUNDANT_CHECK_ELIMINATION_PASS_H
#define MOP_IR_IMPLEMENTATION_REDUNDANT_CHECK_ELIMINATION_PASS_H

#include "../../Pass.h"
#include "../MOPData.h"
#include "../MOPIRUnit.h"
#include <vector>

namespace MopIRImpl {
namespace MOP {

/**
 * 冗余检查消除 Pass
 * 
 * 按照原项目的实现方式，使用支配集问题求解
 * 返回需要保留的 MOP 列表（支配集），不在列表中的 MOP 就是冗余的
 */
class RedundantCheckEliminationPass : public MopIRImpl::Pass {
public:
  bool optimize(MopIRImpl::IRUnit& IR, MopIRImpl::PassContext& Context) override;
  
  const char* getName() const override {
    return "RedundantCheckEliminationPass";
  }
  
  /**
   * 获取支配集（需要保留的 MOP 列表）
   * 
   * 在 optimize() 之后调用，获取消除冗余后的 MOP 列表
   */
  const std::vector<Mop*>& getDominatingSet() const {
    return DominatingSet;
  }
  
  /**
   * 检查 MOP 是否在支配集中（是否需要保留）
   */
  bool shouldKeep(Mop* mop) const {
    return std::find(DominatingSet.begin(), DominatingSet.end(), mop) 
           != DominatingSet.end();
  }

private:
  std::vector<Mop*> DominatingSet;  // 支配集（需要保留的 MOP）
  
  /**
   * 蒸馏冗余检查（类似原项目的 distillRecurringChecks）
   * 
   * @param mops 所有 MOP 列表
   * @param context Pass 上下文
   * @param isTsan 是否用于 TSan（影响写操作条件）
   * @param ignoreCalls 是否忽略函数调用检查
   * @return 需要保留的 MOP 列表（支配集）
   */
  std::vector<Mop*> distillRecurringChecks(
      const MopList& mops,
      MopIRImpl::PassContext& context,
      bool isTsan = false, 
      bool ignoreCalls = false);
};

} // namespace MOP
} // namespace MopIRImpl

#endif // MOP_IR_IMPLEMENTATION_REDUNDANT_CHECK_ELIMINATION_PASS_H

