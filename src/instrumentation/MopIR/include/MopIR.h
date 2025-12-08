#ifndef XSAN_MOP_IR_MOPIR_H
#define XSAN_MOP_IR_MOPIR_H

#include "Mop.h"
#include "MopBuilder.h"
#include "MopContext.h"
#include "MopAnalysis.h"
#include "MopOptimizer.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

// 前向声明
namespace __xsan {
namespace MopIR {
class MopIRAnnotator;
}
}

// 需要 AnnotationLevel 的完整定义（用于默认参数）
#include "MopIRAnnotator.h"

namespace __xsan {
namespace MopIR {

// MopIR 主类：统一管理高阶IR的构建、分析和优化
// 这个类同时作为：
// 1. 高阶IR：提供内存操作的抽象表示
// 2. 优化Pass：可以集成到LLVM Pass管道中
class MopIR {
private:
  MopContext Context;
  MopBuilder Builder;
  MopList Mops;                    // 当前MOP列表
  MopAnalysisPipeline AnalysisPipeline;
  MopOptimizationPipeline OptimizationPipeline;
  
  bool Built;                      // 是否已构建
  bool Analyzed;                   // 是否已分析
  bool Optimized;                  // 是否已优化

public:
  // 从 Function 和 FunctionAnalysisManager 构造
  MopIR(llvm::Function& F, llvm::FunctionAnalysisManager& FAM)
    : Context(F, FAM), Builder(F), Built(false), 
      Analyzed(false), Optimized(false) {
    initializeDefaultPipeline();
  }
  
  // 构建MOP列表（从LLVM IR转换）
  void build();
  
  // 运行分析流水线
  void analyze();
  
  // 运行优化流水线
  void optimize();
  
  // 一次性执行：构建 -> 分析 -> 优化
  void buildAnalyzeAndOptimize() {
    build();
    analyze();
    optimize();
  }
  
  // 获取优化后的MOP列表（用于插桩）
  const MopList& getOptimizedMops() const {
    return Mops;
  }
  
  // 获取原始MOP列表
  const MopList& getMops() const {
    return Mops;
  }
  
  // 获取需要插桩的指令列表（从优化后的MOP提取）
  llvm::SmallVector<llvm::Instruction*, 16> getInstrumentationPoints() const;
  
  // 获取具有特定访问模式的MOP列表（用于插桩优化）
  llvm::SmallVector<Mop*, 16> getMopsWithPattern(AccessPattern Pattern) const;
  
  // 获取所有范围访问的MOP
  llvm::SmallVector<Mop*, 16> getRangeAccessMops() const {
    return getMopsWithPattern(AccessPattern::Range);
  }
  
  // 获取所有周期性访问的MOP
  llvm::SmallVector<Mop*, 16> getPeriodicAccessMops() const {
    return getMopsWithPattern(AccessPattern::Periodic);
  }
  
  // 获取所有循环访问的MOP
  llvm::SmallVector<Mop*, 16> getCyclicAccessMops() const {
    return getMopsWithPattern(AccessPattern::Cyclic);
  }
  
  // 访问器
  MopContext& getContext() { return Context; }
  const MopContext& getContext() const { return Context; }
  
  MopAnalysisPipeline& getAnalysisPipeline() { return AnalysisPipeline; }
  MopOptimizationPipeline& getOptimizationPipeline() { return OptimizationPipeline; }
  
  // 重置状态
  void reset() {
    Mops.clear();
    Built = false;
    Analyzed = false;
    Optimized = false;
  }
  
  // 打印MOP IR信息（用于调试）
  void print(llvm::raw_ostream& OS) const;
  
  // 将 MopIR 信息以注释形式添加到 LLVM IR 中
  // Level: 注释详细级别
  // UseMetadata: 是否使用 Metadata（推荐，否则使用注释字符串）
  void annotateIR(AnnotationLevel Level = AnnotationLevel::Basic, 
                  bool UseMetadata = true);

private:
  // 初始化默认的分析和优化流水线
  void initializeDefaultPipeline();
};

// LLVM Pass 接口：使 MopIR 可以作为 LLVM Pass 运行
class MopIRPass : public llvm::PassInfoMixin<MopIRPass> {
private:
  bool EnableRecurrenceOptimization;
  bool IsTsanMode;
  
public:
  MopIRPass(bool EnableRecurrence = true, bool Tsan = false)
    : EnableRecurrenceOptimization(EnableRecurrence), IsTsanMode(Tsan) {}
  
  llvm::PreservedAnalyses run(llvm::Function& F,
                               llvm::FunctionAnalysisManager& FAM);
  
  static bool isRequired() { return false; }
};

} // namespace MopIR
} // namespace __xsan

#endif // XSAN_MOP_IR_MOPIR_H
