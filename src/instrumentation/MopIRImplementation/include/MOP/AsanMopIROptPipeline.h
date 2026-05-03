#ifndef MOP_IR_ASAN_MOPIR_OPT_PIPELINE_H
#define MOP_IR_ASAN_MOPIR_OPT_PIPELINE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

namespace MopIRImpl {
namespace MOP {

/**
 * ASan 路径上的 MopIR「高阶 IR」优化流水线（当前在 AsanRequireAnalysisPass
 * 中的顺序，均在 collectTargetsToIntrument 完成之后）：
 *
 * 0) XSan 合成器（仅 XSanInstPass / -xsan）：在 Module 上先于各子 Sanitizer
 *    运行 LoopMopInstrumenter（见 XSanitizerCompositor.cpp），由
 *    -mllvm -xsan-loop-opt 控制粒度；与下面 MopIR 两相独立。
 *
 * 1) collectTargetsToIntrument：填充 AsanToInstrument（非 MopIR，但为
 *    后续两相的输入前提）。
 *
 * 2) Phase — Redundancy：对候选 load/store 建 MOPIRUnit 后，经
 *    MopIRImpl::PassManager 运行 RedundantCheckEliminationPass（
 *    MAIR_USE_MOPIR_ASAN_REDUNDANCY）；失败时由调用方回退
 *    MopRecurrenceReducer。见 tryRunAsanMopIRRedundancyPhase。
 *
 * 3) Phase — Loop（runAsanMopIRLoopRelocationPhase）：调用循环不变量外提
 *    实现（MAIR_USE_MOPIR_ASAN_LOOP_RELOC）。若 \p mopIrLoopCandidates 非空，
 *    先据此构建 MOPIRUnit，在外提改写 LLVM IR **之前**对将外提的 MOP 置位
 *    循环外提计划标记，再执行与原先等价的 IR 外提。放在冗余之后，
 *    以便先收缩插桩目标再在 IR 上外提检查；XSan 下若合成器已做循环优化，
 *    调用方应通过 strategy 关闭本相，避免重复改写。
 *
 * @param mopirLoopRelocationStrategyEnabled 是否允许执行 MopIR 外提策略
 *       （由调用方根据 XSAN_PASS、xsan-loop-opt、环境变量等计算）。
 * @param asDebugFuncName 对应 -as-debug-func；非空且等于 F 名时跳过本相。
 * @param mopIrLoopCandidates 用于填充高阶 MOP 单元的 load/store 指令序列；
 *       为空则跳过 MOP 标注（外提行为与未传 MOP 时一致）。
 */
void runAsanMopIRLoopRelocationPhase(
    llvm::Function &F, llvm::FunctionAnalysisManager &FAM,
    bool mopirLoopRelocationStrategyEnabled, llvm::StringRef asDebugFuncName,
    llvm::ArrayRef<const llvm::Instruction *> mopIrLoopCandidates = {});

/**
 * 尝试仅由 MopIR 完成 ASan 周期性检查的冗余蒸馏。
 *
 * @return  true 表示 Out 已由 MopIR 填满且调用方不应再跑原版 reducer；
 *          false 表示未启用编译项或 MopIR 失败，调用方应使用
 *          MopRecurrenceReducer::distillRecurringChecks。
 */
bool tryRunAsanMopIRRedundancyPhase(
    llvm::Function &F, llvm::FunctionAnalysisManager &FAM,
    llvm::ModuleAnalysisManager &MAM,
    llvm::ArrayRef<const llvm::Instruction *> TmpInsts,
    llvm::SmallVectorImpl<const llvm::Instruction *> &Out);

} // namespace MOP
} // namespace MopIRImpl

#endif
