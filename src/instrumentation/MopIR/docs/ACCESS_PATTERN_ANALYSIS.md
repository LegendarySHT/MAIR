# 访问模式分析 (Access Pattern Analysis)

## 概述

访问模式分析是 MopIR 的一个重要功能，用于识别和记录内存操作的访问模式，以便后续的插桩优化能够利用这些信息进行更高效的插桩。

## 支持的访问模式

### 1. 简单访问 (Simple Access)
- **描述**：范围不变的内存访问
- **用途**：标准的单次内存访问检查

### 2. 范围访问 (Range Access)
- **描述**：访问连续范围 `[L, R)`，步长等于访问大小
- **模式**：`[L, R)`，其中步长 = 访问大小
- **用途**：可以使用 `__xsan_read_range` 或 `__xsan_write_range` 进行范围检查

### 3. 周期性访问 (Periodic Access)
- **描述**：按固定步长访问内存范围
- **模式**：`([L, R), AccessSize, Step)`，按步长分割范围
- **用途**：可以使用 `__xsan_period_readX` 或 `__xsan_period_writeX` 进行周期性检查

### 4. 循环访问 (Cyclic Access)
- **描述**：访问两个不连续的范围段
- **模式**：`([L, R), Beg, End)`，访问 `[Beg, R)` 和 `[L, End)`
- **用途**：需要特殊处理，分别检查两个范围段

## 数据结构

### AccessPatternInfo

```cpp
struct AccessPatternInfo {
  AccessPattern Pattern;           // 访问模式类型
  
  // 范围访问
  llvm::Value* RangeBegin;         // L
  llvm::Value* RangeEnd;           // R
  
  // 周期性访问
  llvm::Value* PeriodicBegin;      // L
  llvm::Value* PeriodicEnd;         // R
  uint64_t AccessSize;             // 每次访问的大小
  llvm::Value* Step;               // 步长值
  bool StepIsConstant;             // Step 是否为常量
  uint64_t StepConstant;           // Step 的常量值
  
  // 循环访问
  llvm::Value* CyclicBegin;         // L
  llvm::Value* CyclicEnd;           // R
  llvm::Value* CyclicAccessBeg;     // Beg
  llvm::Value* CyclicAccessEnd;     // End
  
  // 关联信息
  llvm::Loop* AssociatedLoop;       // 关联的循环
  uint64_t MopSize;                 // MOP 的访问大小
};
```

## 使用方法

### 基本使用

```cpp
// 创建 MopIR 并运行分析
MopIR MopIR(F, FAM);
MopIR.buildAnalyzeAndOptimize();

// 获取具有特定模式的 MOP
auto RangeMops = MopIR.getRangeAccessMops();
auto PeriodicMops = MopIR.getPeriodicAccessMops();
auto CyclicMops = MopIR.getCyclicAccessMops();

// 检查单个 MOP 的访问模式
for (auto& Mop : MopIR.getOptimizedMops()) {
  const auto& Pattern = Mop->getAccessPattern();
  
  if (Pattern.isRangeAccess()) {
    // 使用范围访问插桩
    llvm::Value* Begin = Pattern.RangeBegin;
    llvm::Value* End = Pattern.RangeEnd;
    // ... 插桩代码
  } else if (Pattern.isPeriodicAccess()) {
    // 使用周期性访问插桩
    llvm::Value* Begin = Pattern.PeriodicBegin;
    llvm::Value* End = Pattern.PeriodicEnd;
    llvm::Value* Step = Pattern.Step;
    // ... 插桩代码
  } else if (Pattern.isCyclicAccess()) {
    // 使用循环访问插桩
    // ... 插桩代码
  }
}
```

### 自定义分析流水线

```cpp
MopIR MopIR(F, FAM);

// 添加访问模式分析器
auto PatternAnalysis = std::make_unique<MopAccessPatternAnalysis>();
MopIR.getAnalysisPipeline().addAnalysis(std::move(PatternAnalysis));

// 运行分析
MopIR.build();
MopIR.analyze();
```

## 实现细节

### 分析流程

1. **SCEV 分析**：使用 Scalar Evolution 分析循环中的指针表达式
2. **模式识别**：
   - 识别 `SCEVAddRecExpr` 来表示循环中的访问
   - 检查步长是否等于访问大小（范围访问）
   - 检查步长是否为常量（周期性访问）
   - 检查是否为循环访问模式
3. **信息提取**：
   - 提取起始和结束地址
   - 提取步长信息
   - 关联循环信息

### 关键函数

- `analyzeMopAccessPattern()`: 分析单个 MOP 的访问模式
- `tryIdentifyRangeAccess()`: 尝试识别范围访问
- `tryIdentifyPeriodicAccess()`: 尝试识别周期性访问
- `tryIdentifyCyclicAccess()`: 尝试识别循环访问
- `extractAccessRangeFromSCEV()`: 从 SCEV 提取访问范围信息

## 与插桩的集成

访问模式信息可以直接用于优化插桩：

```cpp
// 在插桩代码中使用
void instrumentMop(Mop* M, IRBuilder& IRB) {
  const auto& Pattern = M->getAccessPattern();
  
  if (Pattern.isRangeAccess()) {
    // 范围访问：使用 __xsan_read_range / __xsan_write_range
    IRB.CreateCall(M->isWrite() ? XsanRangeWrite : XsanRangeRead,
                   {Pattern.RangeBegin, Pattern.RangeEnd, PcValue});
  } else if (Pattern.isPeriodicAccess()) {
    // 周期性访问：使用 __xsan_period_readX / __xsan_period_writeX
    size_t Idx = countTrailingZeros(Pattern.AccessSize);
    IRB.CreateCall(M->isWrite() ? XsanPeriodWrite[Idx] : XsanPeriodRead[Idx],
                   {Pattern.PeriodicBegin, Pattern.PeriodicEnd, Pattern.Step, PcValue});
  } else {
    // 标准访问：使用常规插桩
    // ...
  }
}
```

## 限制和注意事项

1. **循环要求**：访问模式分析主要针对循环中的访问
2. **简单循环**：目前只支持简单循环（可分析的循环）
3. **循环访问**：循环访问模式的支持仍在开发中（TODO）
4. **性能**：SCEV 分析可能对大型函数有性能影响

## 未来扩展

1. **更多模式**：支持更多复杂的访问模式
2. **嵌套循环**：支持嵌套循环中的访问模式
3. **动态步长**：支持非常量步长的分析
4. **优化建议**：基于访问模式提供插桩优化建议
