# 访问模式分析功能总结

## 已完成的工作

### 1. 扩展 Mop 类 (`Mop.h`)

#### 新增枚举和结构体
- **AccessPattern 枚举**：定义五种访问模式
  - `Unknown`: 未知模式
  - `Simple`: 简单访问（范围不变）
  - `Range`: 范围访问 `[L, R)`
  - `Periodic`: 周期性访问 `([L, R), AccessSize, Step)`
  - `Cyclic`: 循环访问 `([L, R), Beg, End)`

- **AccessPatternInfo 结构体**：存储访问模式的详细信息
  - 范围访问字段：`RangeBegin`, `RangeEnd`
  - 周期性访问字段：`PeriodicBegin`, `PeriodicEnd`, `AccessSize`, `Step`, `StepIsConstant`, `StepConstant`
  - 循环访问字段：`CyclicBegin`, `CyclicEnd`, `CyclicAccessBeg`, `CyclicAccessEnd`
  - 通用字段：`AssociatedLoop`, `MopSize`
  - 便捷查询方法：`isRangeAccess()`, `isPeriodicAccess()`, `isCyclicAccess()`, `isSimpleAccess()`, `hasKnownPattern()`

#### 扩展 Mop 类方法
- `getAccessPattern()`: 获取访问模式信息
- `setAccessPattern()`: 设置访问模式信息
- `hasKnownAccessPattern()`: 检查是否有已知模式
- `isRangeAccess()`, `isPeriodicAccess()`, `isCyclicAccess()`, `isSimpleAccess()`: 便捷查询方法

### 2. 扩展 MopContext 类 (`MopContext.h`)

- 添加 `ScalarEvolution* SE` 字段
- 添加 `getScalarEvolution()` 方法，支持延迟初始化
- 更新构造函数以支持 ScalarEvolution

### 3. 创建访问模式分析器 (`MopAnalysis.h`)

#### MopAccessPatternAnalysis 类
- **功能**：识别和记录内存访问模式
- **统计信息**：
  - `NumRangeAccesses`: 范围访问数量
  - `NumPeriodicAccesses`: 周期性访问数量
  - `NumCyclicAccesses`: 循环访问数量
  - `NumSimpleAccesses`: 简单访问数量
  - `NumUnknownAccesses`: 未知访问数量

- **核心方法**：
  - `analyze()`: 分析 MOP 列表的访问模式
  - `analyzeMopAccessPattern()`: 分析单个 MOP
  - `tryIdentifyRangeAccess()`: 识别范围访问
  - `tryIdentifyPeriodicAccess()`: 识别周期性访问
  - `tryIdentifyCyclicAccess()`: 识别循环访问
  - `extractAccessRangeFromSCEV()`: 从 SCEV 提取访问范围
  - `isSimpleLoop()`: 检查是否为简单循环
  - `getMopAccessSize()`: 计算 MOP 访问大小

### 4. 扩展 MopIR 主类 (`MopIR.h`)

- 添加便捷方法：
  - `getMopsWithPattern()`: 获取具有特定模式的 MOP
  - `getRangeAccessMops()`: 获取范围访问 MOP
  - `getPeriodicAccessMops()`: 获取周期性访问 MOP
  - `getCyclicAccessMops()`: 获取循环访问 MOP

## 架构设计

### 数据流

```
LLVM IR
  ↓
MopBuilder (构建 MOP 列表)
  ↓
MopAccessPatternAnalysis (分析访问模式)
  ↓
Mop (包含 AccessPatternInfo)
  ↓
插桩优化 (利用访问模式信息)
```

### 访问模式识别流程

1. **SCEV 分析**：使用 Scalar Evolution 分析循环中的指针表达式
2. **模式匹配**：
   - 识别 `SCEVAddRecExpr` 表示循环访问
   - 检查步长与访问大小的关系
   - 区分范围访问、周期性访问和循环访问
3. **信息提取**：
   - 提取起始和结束地址
   - 提取步长信息
   - 关联循环信息
4. **结果存储**：将分析结果存储到 `Mop::PatternInfo`

## 使用示例

### 基本使用

```cpp
// 创建 MopIR
MopIR MopIR(F, FAM);

// 构建、分析和优化
MopIR.buildAnalyzeAndOptimize();

// 获取不同模式的 MOP
auto RangeMops = MopIR.getRangeAccessMops();
auto PeriodicMops = MopIR.getPeriodicAccessMops();

// 检查单个 MOP 的模式
for (auto& Mop : MopIR.getOptimizedMops()) {
  if (Mop->isRangeAccess()) {
    const auto& Pattern = Mop->getAccessPattern();
    // 使用 Pattern.RangeBegin 和 Pattern.RangeEnd
  }
}
```

### 插桩集成

```cpp
void instrumentMop(Mop* M, IRBuilder& IRB) {
  const auto& Pattern = M->getAccessPattern();
  
  if (Pattern.isRangeAccess()) {
    // 范围访问插桩
    IRB.CreateCall(M->isWrite() ? XsanRangeWrite : XsanRangeRead,
                   {Pattern.RangeBegin, Pattern.RangeEnd, PcValue});
  } else if (Pattern.isPeriodicAccess()) {
    // 周期性访问插桩
    size_t Idx = countTrailingZeros(Pattern.AccessSize);
    IRB.CreateCall(M->isWrite() ? XsanPeriodWrite[Idx] : XsanPeriodRead[Idx],
                   {Pattern.PeriodicBegin, Pattern.PeriodicEnd, 
                    Pattern.Step, PcValue});
  }
}
```

## 下一步工作

### 待实现的功能

1. **实现访问模式识别逻辑** (`MopAccessPatternAnalysis.cpp`)
   - 实现 `analyzeMopAccessPattern()` 方法
   - 实现 `tryIdentifyRangeAccess()` 方法
   - 实现 `tryIdentifyPeriodicAccess()` 方法
   - 实现 `tryIdentifyCyclicAccess()` 方法
   - 实现 `extractAccessRangeFromSCEV()` 方法
   - 参考 `Instrumentation.cpp:1313-1440` 的实现

2. **集成到默认流水线**
   - 在 `MopIR::initializeDefaultPipeline()` 中添加 `MopAccessPatternAnalysis`

3. **测试和验证**
   - 测试范围访问识别
   - 测试周期性访问识别
   - 测试循环访问识别
   - 验证与原有插桩逻辑的兼容性

## 参考实现

参考 `Instrumentation.cpp` 中的实现：
- 行 1313-1320: 访问模式定义
- 行 1307-1356: SCEV 分析和模式识别
- 行 1374-1385: 范围访问判断
- 行 1417-1423: 范围提取
- 行 1430-1439: 插桩代码生成

## 注意事项

1. **循环要求**：访问模式分析主要针对循环中的访问
2. **简单循环**：目前只支持简单循环（可分析的循环）
3. **循环访问**：循环访问模式的支持需要特殊处理（参考 Instrumentation.cpp 中的 TODO）
4. **性能考虑**：SCEV 分析可能对大型函数有性能影响，需要适当优化
