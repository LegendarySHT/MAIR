# MopIR 注释功能实现总结

## 已完成的工作

### 1. 创建 MopIRAnnotator 类 (`MopIRAnnotator.h` 和 `MopIRAnnotator.cpp`)

#### 核心功能
- **注释详细级别**：支持 4 种级别（Minimal, Basic, Detailed, Full）
- **注释方式**：支持 Metadata 方式和注释字符串方式
- **自动注释**：可以为整个 MopIR 或单个 MOP 添加注释

#### 主要方法
- `annotate(MopIR&)`: 为整个 MopIR 添加注释
- `annotateMop(Mop*)`: 为单个 MOP 添加注释
- `setAnnotationLevel()` / `getAnnotationLevel()`: 设置/获取注释级别
- `setUseMetadata()` / `getUseMetadata()`: 设置/获取注释方式

#### 内部方法
- `mopTypeToString()`: 将 MOP 类型转换为字符串
- `accessPatternToString()`: 将访问模式转换为字符串
- `generateMopBriefString()`: 生成简要信息字符串
- `generateMopInfoString()`: 生成完整信息字符串
- `annotateWithMetadata()`: 使用 Metadata 添加注释
- `annotateWithComment()`: 使用注释字符串添加注释

### 2. 扩展 MopIR 类 (`MopIR.h`)

- 添加 `annotateIR()` 方法，提供便捷的注释接口
- 使用前向声明避免循环依赖
- 在 `MopIR.cpp` 中实现方法

### 3. Metadata 键名定义

- `xsan.mop.type`: MOP 类型信息
- `xsan.mop.pattern`: 访问模式信息
- `xsan.mop.redundant`: 冗余状态
- `xsan.mop.covering`: 覆盖关系
- `xsan.mop.dependencies`: 依赖关系
- `xsan.mop.access_pattern`: 访问模式详细信息
- `xsan.mop.full_info`: 完整信息字符串
- `xsan.mop.comment`: 注释字符串

## 注释内容

### Minimal 级别
- MOP 类型
- 是否冗余

### Basic 级别
- MOP 类型
- 访问模式
- 是否冗余

### Detailed 级别
- 完整的 MOP 信息字符串（包含所有字段）

### Full 级别
- 所有结构化 Metadata（类型、模式、冗余、依赖、覆盖）
- 完整信息字符串

## 使用示例

### 基本使用

```cpp
MopIR MopIR(F, FAM);
MopIR.buildAnalyzeAndOptimize();

// 添加注释
MopIR.annotateIR();
```

### 自定义级别

```cpp
// 使用完整级别
MopIR.annotateIR(AnnotationLevel::Full);

// 使用最小级别
MopIR.annotateIR(AnnotationLevel::Minimal);
```

### 直接使用注释器

```cpp
MopIRAnnotator Annotator(AnnotationLevel::Full, true);
Annotator.annotate(MopIR);
```

## 生成的 IR 示例

### Basic 级别

```llvm
%1 = load i32, i32* %ptr, !xsan.mop.type !{!"Load", !"Range", !"false"}
```

### Full 级别

```llvm
%1 = load i32, i32* %ptr,
  !xsan.mop.type !{!"Load"},
  !xsan.mop.pattern !{!"Range"},
  !xsan.mop.redundant !{!"false"},
  !xsan.mop.full_info !{!"=== MOP Information ===\nType: Load\n..."}
```

## 架构设计

### 数据流

```
MopIR
  ↓
MopIRAnnotator
  ↓
生成注释信息
  ↓
添加到 LLVM IR Metadata
  ↓
可在 IR 转储时查看
```

### 设计特点

1. **模块化**：注释器独立于 MopIR 核心功能
2. **可配置**：支持多种注释级别和方式
3. **非侵入性**：不影响代码生成
4. **可扩展**：易于添加新的注释信息

## 文件结构

```
MopIR/
├── include/
│   ├── MopIRAnnotator.h      # 注释器头文件
│   └── MopIR.h                # MopIR 主类（已更新）
└── lib/
    ├── MopIRAnnotator.cpp     # 注释器实现
    └── MopIR.cpp              # MopIR 实现（已更新）
```

## 下一步工作

1. **测试**：验证注释功能在各种场景下的正确性
2. **工具支持**：创建工具来解析和显示注释
3. **性能优化**：优化大量 MOP 的注释生成性能
4. **格式扩展**：根据需要添加更多注释信息

## 注意事项

1. **循环依赖**：使用前向声明避免头文件循环依赖
2. **Metadata 持久化**：Metadata 会保留在 IR 中
3. **调试用途**：主要用于调试，不应依赖其存在性
4. **版本兼容**：Metadata 格式可能在将来变化
