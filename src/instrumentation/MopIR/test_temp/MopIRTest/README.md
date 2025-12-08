# MopIR 功能测试

## 概述

这个测试套件用于验证 MopIR 的各项功能是否正常工作，包括：
- 构建功能（从 LLVM IR 构建 MOP 列表）
- 分析功能（别名分析、支配关系分析等）
- 优化功能（赘余检查消除等）
- 访问模式查询
- 插桩位点获取
- 注释功能
- 打印功能

## 文件结构

```
MopIRTest/
├── CMakeLists.txt          # CMake 构建配置
├── MopIRTest.cpp          # 主测试程序
├── test_simple.ll         # 简单测试 IR
├── run_test.sh            # 测试运行脚本
└── README.md              # 本文档
```

## 构建和运行

### 方法 1: 使用脚本（推荐）

```bash
cd /XSan/src/instrumentation/MopIR/test_temp/MopIRTest
./run_test.sh
```

### 方法 2: 手动构建

```bash
cd /XSan/src/instrumentation/MopIR/test_temp/MopIRTest
mkdir -p build
cd build
cmake ..
make -j$(nproc)
./MopIRTest ../test_simple.ll
```

### 方法 3: 使用现有测试文件

```bash
cd /XSan/src/instrumentation/MopIR/test_temp/MopIRTest/build
./MopIRTest ../test_simple.ll
# 或使用 MopConver 目录中的测试文件
./MopIRTest ../../MopConver/simple_test.ll
```

## 测试内容

### 1. 构建测试
- 验证 MopBuilder 能否正确从 LLVM IR 构建 MOP 列表
- 统计不同类型的 MOP（Load/Store/Atomic）

### 2. 分析测试
- 测试别名分析
- 测试支配关系分析
- 验证分析流水线运行

### 3. 优化测试
- 测试赘余检查消除优化
- 统计冗余 MOP 数量

### 4. 访问模式查询测试
- 测试范围访问查询
- 测试周期性访问查询
- 测试简单访问查询

### 5. 插桩位点测试
- 验证插桩位点获取功能
- 确认冗余 MOP 被正确过滤

### 6. 注释测试
- 测试 Metadata 注释添加
- 验证注释功能正常工作

### 7. 打印测试
- 测试 MopIR 信息打印功能

## 预期输出

测试成功时，应该看到类似以下输出：

```
=== MopIR Functionality Test ===
Input file: test_simple.ll
Module: test_simple

------------------------------------------------------------
Testing function: test_simple
------------------------------------------------------------

=== Testing Build Function ===
✓ Build successful
  Total MOPs: 3
  Load MOPs: 2
  Store MOPs: 1
  Atomic MOPs: 0

=== Testing Analysis Function ===
✓ Analysis successful

=== Testing Optimization Function ===
✓ Optimization successful
  Redundant MOPs: 0

=== Testing Access Pattern Queries ===
✓ Access pattern queries successful
  Range Access MOPs: 0
  Periodic Access MOPs: 0
  Simple Access MOPs: 0

=== Testing Instrumentation Points ===
✓ Instrumentation points query successful
  Instrumentation Points: 3
  (Redundant MOPs excluded: 0)

=== Testing Annotation Function ===
✓ Annotation successful
  Metadata added to IR

=== Testing Print Function ===
=== MopIR Information ===
...

============================================================
Test Summary
============================================================
Build:        ✓ PASS
Analyze:      ✓ PASS
Optimize:     ✓ PASS
Annotation:   ✓ PASS

Statistics:
  Total MOPs:              3
  Load MOPs:               2
  Store MOPs:              1
  ...
============================================================
```

## 故障排除

### 编译错误

如果遇到编译错误，检查：
1. LLVM 是否正确安装
2. 头文件路径是否正确
3. 链接库是否完整

### 运行时错误

如果遇到运行时错误，检查：
1. 输入 IR 文件是否存在
2. IR 文件格式是否正确
3. FunctionAnalysisManager 是否正确初始化

### 测试失败

如果某个测试失败：
1. 查看错误信息
2. 检查相关功能的实现
3. 参考 `IMPLEMENTATION_STATUS.md` 了解实现状态

## 扩展测试

### 添加新的测试用例

1. 创建新的 IR 文件（如 `test_advanced.ll`）
2. 在 `MopIRTest.cpp` 中添加新的测试函数
3. 在 `main()` 中调用新测试

### 测试特定功能

可以修改测试代码来测试特定功能：

```cpp
// 只测试构建功能
testBuild(MopIR, Results);

// 只测试特定分析器
auto CustomAnalysis = std::make_unique<MyCustomAnalysis>();
MopIR.getAnalysisPipeline().addAnalysis(std::move(CustomAnalysis));
MopIR.analyze();
```

## 注意事项

1. **依赖关系**：确保所有分析在优化之前完成
2. **性能**：大量 MOP 可能影响测试性能
3. **正确性**：测试主要验证功能是否运行，不验证结果正确性
4. **实现状态**：某些功能可能还在开发中，参考 `IMPLEMENTATION_STATUS.md`

## 相关文档

- `QUICK_START.md` - 快速开始指南
- `IMPLEMENTATION_STATUS.md` - 实现状态
- `ANNOTATION_GUIDE.md` - 注释功能指南
