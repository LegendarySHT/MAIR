#include "../include/MopIR.h"
#include "../include/MopIRAnnotator.h"
#include "../include/MopAnalysis.h"
#include "../include/MopOptimizer.h"
#include "llvm/Support/raw_ostream.h"

using namespace __xsan::MopIR;
using namespace llvm;

// 构建MOP列表（从LLVM IR转换）
void MopIR::build() {
  if (Built) {
    return;  // 已经构建过，跳过
  }

  // 使用 Builder 构建 MOP 列表
  Mops = Builder.buildMopList();
  Built = true;
}

// 运行分析流水线
void MopIR::analyze() {
  if (!Built) {
    build();  // 如果还没构建，先构建
  }
  
  if (Analyzed) {
    return;  // 已经分析过，跳过
  }
  
  // 设置上下文
  AnalysisPipeline.setContext(Context);
  
  // 运行分析流水线
  AnalysisPipeline.run(Mops);
  
  Analyzed = true;
}

// 运行优化流水线
void MopIR::optimize() {
  if (!Analyzed) {
    analyze();  // 如果还没分析，先分析
  }
  
  if (Optimized) {
    return;  // 已经优化过，跳过
  }
  
  // 设置上下文
  OptimizationPipeline.setContext(Context);
  
  // 运行优化流水线
  OptimizationPipeline.run(Mops);
  
  Optimized = true;
}

// 获取需要插桩的指令列表
SmallVector<Instruction*, 16> MopIR::getInstrumentationPoints() const {
  SmallVector<Instruction*, 16> Points;
  
  for (const auto& Mop : Mops) {
    // 只包含非冗余的 MOP
    if (!Mop->isRedundant() && Mop->getOriginalInst()) {
      Points.push_back(Mop->getOriginalInst());
    }
  }
  
  return Points;
}

// 获取具有特定访问模式的MOP列表
SmallVector<Mop*, 16> MopIR::getMopsWithPattern(AccessPattern Pattern) const {
  SmallVector<Mop*, 16> Result;
  
  for (const auto& Mop : Mops) {
    if (!Mop->isRedundant() && Mop->getAccessPattern().Pattern == Pattern) {
      Result.push_back(Mop.get());
    }
  }
  
  return Result;
}

// 打印MOP IR信息（用于调试）
void MopIR::print(raw_ostream& OS) const {
  OS << "=== MopIR Information ===\n";
  OS << "Function: " << Context.getFunction().getName() << "\n";
  OS << "Total MOPs: " << Mops.size() << "\n";
  OS << "Built: " << (Built ? "Yes" : "No") << "\n";
  OS << "Analyzed: " << (Analyzed ? "Yes" : "No") << "\n";
  OS << "Optimized: " << (Optimized ? "Yes" : "No") << "\n\n";
  
  // 统计信息
  unsigned NumLoads = 0, NumStores = 0, NumAtomic = 0;
  unsigned NumRedundant = 0;
  unsigned NumRange = 0, NumPeriodic = 0, NumCyclic = 0, NumSimple = 0;
  
  for (const auto& Mop : Mops) {
    switch (Mop->getType()) {
      case MopType::Load: NumLoads++; break;
      case MopType::Store: NumStores++; break;
      case MopType::Atomic: NumAtomic++; break;
      default: break;
    }
    
    if (Mop->isRedundant()) {
      NumRedundant++;
    }
    
    const auto& Pattern = Mop->getAccessPattern();
    switch (Pattern.Pattern) {
      case AccessPattern::Range: NumRange++; break;
      case AccessPattern::Periodic: NumPeriodic++; break;
      case AccessPattern::Cyclic: NumCyclic++; break;
      case AccessPattern::Simple: NumSimple++; break;
      default: break;
    }
  }
  
  OS << "MOP Statistics:\n";
  OS << "  Loads: " << NumLoads << "\n";
  OS << "  Stores: " << NumStores << "\n";
  OS << "  Atomic: " << NumAtomic << "\n";
  OS << "  Redundant: " << NumRedundant << "\n";
  OS << "  Range Access: " << NumRange << "\n";
  OS << "  Periodic Access: " << NumPeriodic << "\n";
  OS << "  Cyclic Access: " << NumCyclic << "\n";
  OS << "  Simple Access: " << NumSimple << "\n\n";
  
  // 打印每个 MOP 的详细信息
  OS << "MOP Details:\n";
  for (size_t i = 0; i < Mops.size(); ++i) {
    OS << "--- MOP #" << i << " ---\n";
    Mops[i]->print(OS);
    OS << "\n";
  }
  
  OS << "========================\n";
}

// 初始化默认的分析和优化流水线
void MopIR::initializeDefaultPipeline() {
  // 设置上下文
  AnalysisPipeline.setContext(Context);
  OptimizationPipeline.setContext(Context);
  
  // 注意：具体的分析器和优化器需要在实际使用时添加
  // 这里只是初始化流水线结构
}

// 实现 annotateIR 方法（避免循环依赖）
void MopIR::annotateIR(AnnotationLevel Level, bool UseMetadata) {
  MopIRAnnotator Annotator(Level, UseMetadata);
  Annotator.annotate(*this);
}
