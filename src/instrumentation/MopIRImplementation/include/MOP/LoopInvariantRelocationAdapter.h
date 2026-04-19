#ifndef MOP_IR_LOOP_INVARIANT_RELOCATION_ADAPTER_H
#define MOP_IR_LOOP_INVARIANT_RELOCATION_ADAPTER_H

#ifdef MOP_IR_USE_LLVM

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

namespace MopIRImpl {
namespace MOP {

/**
 * 执行与原版 LoopMopInstrumenter 中 RelocateInvariantChecks 等价的
 * 「不变量检查外提」。在 AsanRequireAnalysisPass 中由
 * AsanMopIROptPipeline 调度，位于 collectTargets 与冗余消除之后（见
 * AsanMopIROptPipeline.h）。
 *
 * 实现位于 MopIRImplementation/lib/MOP/LLVM/LoopInvariantRelocation.cpp，
 * 不依赖 Instrumentation.h / LoopMopInstrumenter；仍使用相同 runtime
 *（__xsan_read* / __xsan_write*）与元数据名 xsan.delegate，便于在原工程对比验证。
 *
 * 注意：XSanInstPass（定义了 XSAN_PASS）下合成器已做过循环优化，调用方
 * 不应再调用本函数，以免重复改写 IR。
 */
void runLoopInvariantRelocationOnly(llvm::Function &F,
                                    llvm::FunctionAnalysisManager &FAM);

} // namespace MOP
} // namespace MopIRImpl

#endif // MOP_IR_USE_LLVM

#endif
