#ifndef MOP_IR_IMPLEMENTATION_CONTIGUOUS_MOP_MERGER_PASS_H
#define MOP_IR_IMPLEMENTATION_CONTIGUOUS_MOP_MERGER_PASS_H

#include "../../Pass.h"
#include "../MOPData.h"
#include "../MOPIRUnit.h"

namespace MopIRImpl {
namespace MOP {

/**
 * 连续 MOP 合并 Pass
 * 
 * 合并相邻的、可以合并的 MOP
 * 例如：((ptr, 4), read) 和 ((ptr+4, 4), read) 可以合并为 ((ptr, 8), read)
 */
class ContiguousMopMergerPass : public MopIRImpl::Pass {
public:
  bool optimize(MopIRImpl::IRUnit& IR, MopIRImpl::PassContext& Context) override {
    if (IR.getIRUnitKind() != MopIRImpl::IRUnit::Kind::MOP)
      return false;
    auto& mopIR = static_cast<MOPIRUnit&>(IR);
    if (!mopIR.isValid()) {
      return false;
    }

    auto& Mops = mopIR.getMops();
    bool modified = false;
    
    // 遍历 MOP，查找可以合并的连续 MOP
    for (size_t i = 0; i + 1 < Mops.size(); ++i) {
      if (canMerge(Mops[i].get(), Mops[i + 1].get(), Context)) {
        // 合并逻辑
        // ...
        modified = true;
      }
    }
    
    return modified;
  }
  
  const char* getName() const override {
    return "ContiguousMopMergerPass";
  }
  
private:
  // 检查两个 MOP 是否可以合并
  bool canMerge(Mop* Mop1, Mop* Mop2, MopIRImpl::PassContext& Context);
};

} // namespace MOP
} // namespace MopIRImpl

#endif // MOP_IR_IMPLEMENTATION_CONTIGUOUS_MOP_MERGER_PASS_H

