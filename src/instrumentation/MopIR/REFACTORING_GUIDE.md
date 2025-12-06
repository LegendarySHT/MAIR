# MopIR 重构指南

## 概述

本次重构将 `MopRecurrenceReducer` 的功能整合到 `MopIR` 中，使 `MopIR` 同时作为：
1. **高阶IR**：提供内存操作的抽象表示
2. **优化Pass**：可以集成到LLVM Pass管道中，为sanitizer提供优化后的插桩位点

## 架构设计

### 核心组件

#### 1. Mop 类 (`Mop.h`)
- **扩展功能**：
  - 添加 `isWrite()` 和 `isRead()` 方法
  - 添加冗余标记字段 `IsRedundant` 和 `CoveringMop`
  - 支持标记和查询冗余关系

#### 2. MopContext 类 (`MopContext.h`) - 新增
- **功能**：统一管理所有分析和优化所需的上下文信息
- **包含**：
  - FunctionAnalysisManager 引用
  - 分析结果缓存（AA, DT, PDT, LI, TLI, DL）
  - 延迟初始化机制

#### 3. MopRecurrenceOptimizer 类 (`MopOptimizer.h`) - 新增
- **功能**：整合 `MopRecurrenceReducer` 的核心逻辑
- **特性**：
  - 支持 TSan 和 ASan 模式
  - 可配置是否忽略调用检查
  - 实现赘余检查识别和消除

#### 4. MopAnalysis 扩展 (`MopAnalysis.h`)
- **新增分析器**：
  - `MopDominanceAnalysis`：支配关系分析
  - 扩展 `MopRedundancyAnalysis`：支持赘余检查分析
- **分析流水线**：`MopAnalysisPipeline` 支持多个分析器组合

#### 5. MopIR 主类 (`MopIR.h`) - 新增
- **功能**：统一入口，管理整个 MopIR 生命周期
- **流程**：
  1. `build()`: 从 LLVM IR 构建 MOP 列表
  2. `analyze()`: 运行分析流水线
  3. `optimize()`: 运行优化流水线
  4. `getInstrumentationPoints()`: 获取优化后的插桩位点

#### 6. MopIRPass (`MopIR.h`) - 新增
- **功能**：LLVM Pass 接口，使 MopIR 可以集成到 Pass 管道

## 使用方式

### 基本使用

```cpp
// 在 Pass 中使用
llvm::FunctionAnalysisManager& FAM = ...;
MopIR MopIR(F, FAM);

// 构建、分析和优化
MopIR.buildAnalyzeAndOptimize();

// 获取优化后的插桩位点
auto Points = MopIR.getInstrumentationPoints();
```

### 自定义优化流水线

```cpp
MopIR MopIR(F, FAM);

// 添加自定义优化器
auto RecurrenceOpt = std::make_unique<MopRecurrenceOptimizer>(/*IsTsan=*/true);
MopIR.getOptimizationPipeline().addOptimizer(std::move(RecurrenceOpt));

// 运行
MopIR.build();
MopIR.optimize();
```

### 作为 LLVM Pass

```cpp
// 在 PassManager 中注册
FAM.registerPass([]() { return MopIRPass(/*EnableRecurrence=*/true, /*IsTsan=*/false); });
```

## 重构优势

1. **模块化设计**：
   - 清晰的职责分离（构建、分析、优化）
   - 易于扩展新的优化和分析

2. **可扩展性**：
   - 优化器和分析器采用插件式设计
   - 流水线模式支持灵活组合

3. **统一接口**：
   - 单一入口点 `MopIR` 类
   - 统一的上下文管理

4. **向后兼容**：
   - 保留原有 `MopRecurrenceReducer` 的功能
   - 可以逐步迁移现有代码

## 后续优化预留空间

1. **新的优化器**：
   - 连续读取合并 (`ContiguousReadMerger`)
   - 冗余写入消除 (`RedundantWriteEliminator`)
   - 其他自定义优化器

2. **新的分析器**：
   - 别名分析增强
   - 数据流分析
   - 循环优化分析

3. **Pass 集成**：
   - 支持不同的 Pass 管道位置
   - 支持条件优化（基于函数特征）

## 实现注意事项

1. **性能**：
   - 使用缓存避免重复分析
   - 延迟初始化减少开销

2. **正确性**：
   - 保持与原有 `MopRecurrenceReducer` 相同的优化逻辑
   - 确保优化后的插桩位点正确

3. **测试**：
   - 需要验证优化结果与原有实现一致
   - 需要测试不同模式（TSan/ASan）

## 迁移路径

1. **阶段1**：实现新的 MopIR 结构（当前阶段）
2. **阶段2**：实现核心优化器（MopRecurrenceOptimizer）
3. **阶段3**：集成测试和验证
4. **阶段4**：逐步替换原有 `MopRecurrenceReducer` 的使用
5. **阶段5**：添加新的优化和分析功能
