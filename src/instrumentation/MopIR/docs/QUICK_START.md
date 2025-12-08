# MopIR 快速开始指南

## 基本使用

### 1. 创建 MopIR 实例

```cpp
#include "MopIR.h"

// 在 Pass 中
void MyPass::runOnFunction(Function& F, FunctionAnalysisManager& FAM) {
  // 创建 MopIR
  MopIR MopIR(F, FAM);
  
  // 构建、分析和优化
  MopIR.buildAnalyzeAndOptimize();
  
  // 现在可以使用 MopIR 了
}
```

### 2. 获取插桩位点

```cpp
// 获取需要插桩的指令列表（已过滤冗余 MOP）
auto Points = MopIR.getInstrumentationPoints();

for (Instruction* Inst : Points) {
  // 在这里添加插桩代码
  // ...
}
```

### 3. 按访问模式查询 MOP

```cpp
// 获取范围访问的 MOP
auto RangeMops = MopIR.getRangeAccessMops();

// 获取周期性访问的 MOP
auto PeriodicMops = MopIR.getPeriodicAccessMops();

// 获取循环访问的 MOP
auto CyclicMops = MopIR.getCyclicAccessMops();

// 自定义模式查询
auto SimpleMops = MopIR.getMopsWithPattern(AccessPattern::Simple);
```

### 4. 添加注释到 IR

```cpp
// 添加基本注释
MopIR.annotateIR();

// 添加详细注释
MopIR.annotateIR(AnnotationLevel::Full);

// 使用注释字符串方式
MopIR.annotateIR(AnnotationLevel::Basic, /*UseMetadata=*/false);
```

### 5. 打印调试信息

```cpp
// 打印 MopIR 信息
MopIR.print(errs());
```

## 自定义分析流水线

```cpp
MopIR MopIR(F, FAM);

// 添加自定义分析器
auto AliasAnalysis = std::make_unique<MopAliasAnalysis>();
MopIR.getAnalysisPipeline().addAnalysis(std::move(AliasAnalysis));

auto DomAnalysis = std::make_unique<MopDominanceAnalysis>();
MopIR.getAnalysisPipeline().addAnalysis(std::move(DomAnalysis));

// 运行分析
MopIR.build();
MopIR.analyze();
```

## 自定义优化流水线

```cpp
MopIR MopIR(F, FAM);

// 添加优化器
auto RecurrenceOpt = std::make_unique<MopRecurrenceOptimizer>(/*IsTsan=*/false);
MopIR.getOptimizationPipeline().addOptimizer(std::move(RecurrenceOpt));

// 运行优化
MopIR.build();
MopIR.analyze();
MopIR.optimize();
```

## 访问 MOP 信息

```cpp
for (const auto& Mop : MopIR.getOptimizedMops()) {
  // 检查类型
  if (Mop->getType() == MopType::Load) {
    // 处理 Load
  }
  
  // 检查是否冗余
  if (Mop->isRedundant()) {
    // 跳过冗余 MOP
    continue;
  }
  
  // 检查访问模式
  if (Mop->isRangeAccess()) {
    const auto& Pattern = Mop->getAccessPattern();
    // 使用 Pattern.RangeBegin 和 Pattern.RangeEnd
  }
  
  // 获取原始指令
  Instruction* Inst = Mop->getOriginalInst();
}
```

## 完整示例

```cpp
#include "MopIR.h"

void instrumentFunction(Function& F, FunctionAnalysisManager& FAM) {
  // 1. 创建 MopIR
  MopIR MopIR(F, FAM);
  
  // 2. 构建、分析和优化
  MopIR.buildAnalyzeAndOptimize();
  
  // 3. 添加注释（用于调试）
  MopIR.annotateIR(AnnotationLevel::Basic);
  
  // 4. 获取插桩位点
  auto Points = MopIR.getInstrumentationPoints();
  
  // 5. 按模式处理
  auto RangeMops = MopIR.getRangeAccessMops();
  for (Mop* M : RangeMops) {
    // 使用范围访问插桩
    const auto& Pattern = M->getAccessPattern();
    // ...
  }
  
  // 6. 处理其他 MOP
  for (Instruction* Inst : Points) {
    // 标准插桩
    // ...
  }
}
```

## 注意事项

1. **构建顺序**：必须先 `build()`，然后 `analyze()`，最后 `optimize()`
2. **上下文依赖**：确保 FunctionAnalysisManager 包含所需的分析
3. **性能**：大量 MOP 可能影响性能，考虑分批处理
4. **冗余 MOP**：使用 `getInstrumentationPoints()` 会自动过滤冗余 MOP

## 下一步

- 查看 `IMPLEMENTATION_STATUS.md` 了解实现状态
- 查看 `ANNOTATION_GUIDE.md` 了解注释功能
- 查看 `REFACTORING_GUIDE.md` 了解架构设计
