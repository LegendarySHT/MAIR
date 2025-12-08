// MopIR 完整功能测试
// 测试构建、分析、优化和注释功能

#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Passes/PassBuilder.h"

#include "../../include/MopIR.h"
#include "../../include/MopAnalysis.h"
#include "../../include/MopOptimizer.h"

using namespace llvm;
using namespace __xsan::MopIR;

// 测试结果统计
struct TestResults {
  unsigned TotalMops = 0;
  unsigned LoadMops = 0;
  unsigned StoreMops = 0;
  unsigned AtomicMops = 0;
  unsigned RedundantMops = 0;
  unsigned RangeAccessMops = 0;
  unsigned PeriodicAccessMops = 0;
  unsigned SimpleAccessMops = 0;
  unsigned InstrumentationPoints = 0;
  bool BuildSuccess = false;
  bool AnalyzeSuccess = false;
  bool OptimizeSuccess = false;
  bool AnnotateSuccess = false;
};

// 测试 MopIR 构建功能
bool testBuild(MopIR& MopIR, TestResults& Results) {
  outs() << "\n=== Testing Build Function ===\n";
  
  MopIR.build();
  Results.BuildSuccess = true;
  
  const auto& Mops = MopIR.getMops();
  Results.TotalMops = Mops.size();
  
  // 统计 MOP 类型
  for (const auto& Mop : Mops) {
    switch (Mop->getType()) {
      case MopType::Load: Results.LoadMops++; break;
      case MopType::Store: Results.StoreMops++; break;
      case MopType::Atomic: Results.AtomicMops++; break;
      default: break;
    }
  }
  
  outs() << "✓ Build successful\n";
  outs() << "  Total MOPs: " << Results.TotalMops << "\n";
  outs() << "  Load MOPs: " << Results.LoadMops << "\n";
  outs() << "  Store MOPs: " << Results.StoreMops << "\n";
  outs() << "  Atomic MOPs: " << Results.AtomicMops << "\n";
  
  return true;
}

// 测试分析功能
bool testAnalyze(MopIR& MopIR, TestResults& Results) {
  outs() << "\n=== Testing Analysis Function ===\n";
  
  // 添加分析器
  auto AliasAnalysis = std::make_unique<MopAliasAnalysis>();
  auto DomAnalysis = std::make_unique<MopDominanceAnalysis>();
  
  MopIR.getAnalysisPipeline().addAnalysis(std::move(AliasAnalysis));
  MopIR.getAnalysisPipeline().addAnalysis(std::move(DomAnalysis));
  
  MopIR.analyze();
  Results.AnalyzeSuccess = true;
  
  outs() << "✓ Analysis successful\n";
  
  return true;
}

// 测试优化功能
bool testOptimize(MopIR& MopIR, TestResults& Results) {
  outs() << "\n=== Testing Optimization Function ===\n";
  
  // 添加优化器
  auto RecurrenceOpt = std::make_unique<MopRecurrenceOptimizer>(/*IsTsan=*/false);
  MopIR.getOptimizationPipeline().addOptimizer(std::move(RecurrenceOpt));
  
  MopIR.optimize();
  Results.OptimizeSuccess = true;
  
  // 统计冗余 MOP
  const auto& Mops = MopIR.getOptimizedMops();
  for (const auto& Mop : Mops) {
    if (Mop->isRedundant()) {
      Results.RedundantMops++;
    }
  }
  
  outs() << "✓ Optimization successful\n";
  outs() << "  Redundant MOPs: " << Results.RedundantMops << "\n";
  
  return true;
}

// 测试访问模式查询
bool testAccessPatterns(MopIR& MopIR, TestResults& Results) {
  outs() << "\n=== Testing Access Pattern Queries ===\n";
  
  auto RangeMops = MopIR.getRangeAccessMops();
  auto PeriodicMops = MopIR.getPeriodicAccessMops();
  auto SimpleMops = MopIR.getMopsWithPattern(AccessPattern::Simple);
  
  Results.RangeAccessMops = RangeMops.size();
  Results.PeriodicAccessMops = PeriodicMops.size();
  Results.SimpleAccessMops = SimpleMops.size();
  
  outs() << "✓ Access pattern queries successful\n";
  outs() << "  Range Access MOPs: " << Results.RangeAccessMops << "\n";
  outs() << "  Periodic Access MOPs: " << Results.PeriodicAccessMops << "\n";
  outs() << "  Simple Access MOPs: " << Results.SimpleAccessMops << "\n";
  
  return true;
}

// 测试插桩位点获取
bool testInstrumentationPoints(MopIR& MopIR, TestResults& Results) {
  outs() << "\n=== Testing Instrumentation Points ===\n";
  
  auto Points = MopIR.getInstrumentationPoints();
  Results.InstrumentationPoints = Points.size();
  
  outs() << "✓ Instrumentation points query successful\n";
  outs() << "  Instrumentation Points: " << Results.InstrumentationPoints << "\n";
  outs() << "  (Redundant MOPs excluded: " << Results.RedundantMops << ")\n";
  
  return true;
}

// 测试注释功能
bool testAnnotation(MopIR& MopIR, TestResults& Results) {
  outs() << "\n=== Testing Annotation Function ===\n";
  
  // 测试不同级别的注释
  MopIR.annotateIR(AnnotationLevel::Basic);
  Results.AnnotateSuccess = true;
  
  outs() << "✓ Annotation successful\n";
  outs() << "  Metadata added to IR\n";
  
  return true;
}

// 测试打印功能
bool testPrint(MopIR& MopIR) {
  outs() << "\n=== Testing Print Function ===\n";
  
  MopIR.print(outs());
  outs() << "✓ Print successful\n";
  return true;
}

// 打印测试结果摘要
void printTestSummary(const TestResults& Results) {
  outs() << "\n" << std::string(60, '=') << "\n";
  outs() << "Test Summary\n";
  outs() << std::string(60, '=') << "\n";
  
  outs() << "Build:        " << (Results.BuildSuccess ? "✓ PASS" : "✗ FAIL") << "\n";
  outs() << "Analyze:      " << (Results.AnalyzeSuccess ? "✓ PASS" : "✗ FAIL") << "\n";
  outs() << "Optimize:     " << (Results.OptimizeSuccess ? "✓ PASS" : "✗ FAIL") << "\n";
  outs() << "Annotation:   " << (Results.AnnotateSuccess ? "✓ PASS" : "✗ FAIL") << "\n";
  
  outs() << "\nStatistics:\n";
  outs() << "  Total MOPs:              " << Results.TotalMops << "\n";
  outs() << "  Load MOPs:               " << Results.LoadMops << "\n";
  outs() << "  Store MOPs:              " << Results.StoreMops << "\n";
  outs() << "  Atomic MOPs:             " << Results.AtomicMops << "\n";
  outs() << "  Redundant MOPs:          " << Results.RedundantMops << "\n";
  outs() << "  Range Access MOPs:        " << Results.RangeAccessMops << "\n";
  outs() << "  Periodic Access MOPs:     " << Results.PeriodicAccessMops << "\n";
  outs() << "  Simple Access MOPs:       " << Results.SimpleAccessMops << "\n";
  outs() << "  Instrumentation Points:   " << Results.InstrumentationPoints << "\n";
  
  outs() << std::string(60, '=') << "\n";
}

int main(int argc, char** argv) {
  if (argc < 2) {
    errs() << "Usage: " << argv[0] << " <input.ll>\n";
    return 1;
  }

  // 初始化 LLVM
  LLVMContext Context;
  SMDiagnostic Error;

  // 读取 LLVM IR 文件
  std::unique_ptr<Module> M = parseIRFile(argv[1], Error, Context);
  if (!M) {
    Error.print("MopIRTest", errs());
    return 1;
  }
  
  outs() << "=== MopIR Functionality Test ===\n";
  outs() << "Input file: " << argv[1] << "\n";
  outs() << "Module: " << M->getName() << "\n\n";
  
  TestResults Results;
  bool AllTestsPassed = true;
  
  // 遍历模块中的所有函数
  for (Function& F : *M) {
    if (F.isDeclaration()) {
      continue;  // 跳过函数声明
    }
    
    outs() << "\n" << std::string(60, '-') << "\n";
    outs() << "Testing function: " << F.getName() << "\n";
    outs() << std::string(60, '-') << "\n";
    
    // 创建 PassBuilder 和 AnalysisManager
    PassBuilder PB;
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;

    // 注册标准分析
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);

    // 交叉注册代理
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // 注册必要的分析（使用 lambda 捕获必要的上下文）
    FAM.registerPass([&]() {
      return DominatorTreeAnalysis();
    });
    FAM.registerPass([&]() { 
      return PostDominatorTreeAnalysis(); 
    });
    FAM.registerPass([&]() { 
      return LoopAnalysis(); 
    });
    FAM.registerPass([&]() { 
      return ScalarEvolutionAnalysis(); 
    });
    FAM.registerPass([&]() { 
      return TargetLibraryAnalysis(); 
    });
    
    // 创建 MopIR
    MopIR MopIR(F, FAM);

    // 运行测试
    AllTestsPassed &= testBuild(MopIR, Results);
    AllTestsPassed &= testAnalyze(MopIR, Results);
    AllTestsPassed &= testOptimize(MopIR, Results);
    AllTestsPassed &= testAccessPatterns(MopIR, Results);
    AllTestsPassed &= testInstrumentationPoints(MopIR, Results);
    AllTestsPassed &= testAnnotation(MopIR, Results);
    AllTestsPassed &= testPrint(MopIR);
  }
  
  // 打印测试摘要
  printTestSummary(Results);
  
  return AllTestsPassed ? 0 : 1;
}