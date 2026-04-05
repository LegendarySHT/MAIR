#ifndef MOP_IR_ASAN_REDUNDANCY_ADAPTER_H
#define MOP_IR_ASAN_REDUNDANCY_ADAPTER_H

#ifdef MOP_IR_USE_LLVM

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/PassManager.h"

namespace MopIRImpl {
namespace MOP {

/**
 * 使用 MopIRImplementation 的 RedundantCheckEliminationPass，对 ASan 的
 * interesting Load/Store 指令列表做与 MopRecurrenceReducer::distillRecurringChecks
 * 同级的蒸馏（IsTsan=false）。
 *
 * @param TmpInsts 与 AsanRequireAnalysisPass 中 filter 后的 Load/Store 序列一致
 * @param Out 输出保留的指令（顺序为 MOPIRUnit 内 Mop 顺序的子序列）
 * @return 是否成功完成（失败时调用方应回退到原版 reducer）
 */
bool runAsanRedundancyReduction(llvm::Function &F,
                                llvm::FunctionAnalysisManager &FAM,
                                llvm::ModuleAnalysisManager &MAM,
                                llvm::ArrayRef<const llvm::Instruction *> TmpInsts,
                                llvm::SmallVectorImpl<const llvm::Instruction *> &Out);

} // namespace MOP
} // namespace MopIRImpl

#endif // MOP_IR_USE_LLVM

#endif // MOP_IR_ASAN_REDUNDANCY_ADAPTER_H
