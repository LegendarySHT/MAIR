#ifndef MOP_IR_LOOP_INVARIANT_RELOCATION_LLVM_WRAPPER_PASS_H
#define MOP_IR_LOOP_INVARIANT_RELOCATION_LLVM_WRAPPER_PASS_H

#include "Pass.h"

namespace MopIRImpl {
namespace MOP {

/**
 * 将 runLoopInvariantRelocationOnly 挂到 MopIRImpl::PassManager /
 * LLVM::LLVMPassManager 流水线上：输入为 LLVM::FunctionIRUnit，
 * 上下文须为 LLVM::LLVMPassContext（含 FAM）。
 */
class LoopInvariantRelocationLLVMWrapperPass : public MopIRImpl::Pass {
public:
  bool optimize(MopIRImpl::IRUnit &IR, MopIRImpl::PassContext &Context) override;
  const char *getName() const override {
    return "LoopInvariantRelocationLLVMWrapperPass";
  }
};

} // namespace MOP
} // namespace MopIRImpl

#endif
