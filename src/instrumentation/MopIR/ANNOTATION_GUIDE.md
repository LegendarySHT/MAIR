# MopIR 注释功能使用指南

## 概述

MopIR 注释功能可以将 MopIR 的分析结果以 Metadata 形式添加到 LLVM IR 中，方便调试和分析。这些注释不会影响代码生成，但可以在 IR 转储时查看。

## 功能特性

### 注释详细级别

1. **Minimal（最小）**：仅包含 MOP 类型和是否冗余
2. **Basic（基本）**：包含类型、访问模式和冗余状态
3. **Detailed（详细）**：包含完整的 MOP 信息字符串
4. **Full（完整）**：包含所有信息，包括依赖关系和覆盖关系

### 注释方式

- **Metadata 方式**（推荐）：使用 LLVM Metadata 系统，结构化存储信息
- **注释字符串方式**：将信息作为字符串存储在 Metadata 中

## 使用方法

### 基本使用

```cpp
// 创建 MopIR
MopIR MopIR(F, FAM);

// 构建、分析和优化
MopIR.buildAnalyzeAndOptimize();

// 添加注释（使用默认的 Basic 级别和 Metadata 方式）
MopIR.annotateIR();
```

### 自定义注释级别

```cpp
// 使用最小级别
MopIR.annotateIR(AnnotationLevel::Minimal);

// 使用详细级别
MopIR.annotateIR(AnnotationLevel::Detailed);

// 使用完整级别
MopIR.annotateIR(AnnotationLevel::Full);
```

### 使用注释字符串方式

```cpp
// 使用注释字符串而不是结构化 Metadata
MopIR.annotateIR(AnnotationLevel::Basic, /*UseMetadata=*/false);
```

### 直接使用注释器

```cpp
// 创建注释器
MopIRAnnotator Annotator(AnnotationLevel::Full, /*UseMetadata=*/true);

// 为整个 MopIR 添加注释
Annotator.annotate(MopIR);

// 或为单个 MOP 添加注释
Mop* SomeMop = ...;
Annotator.annotateMop(SomeMop);
```

## Metadata 格式

### Minimal 级别

```llvm
%1 = load i32, i32* %ptr, !xsan.mop.type !{!"Load", !"false"}
```

### Basic 级别

```llvm
%1 = load i32, i32* %ptr, !xsan.mop.type !{!"Load", !"Range", !"false"}
```

### Full 级别

```llvm
%1 = load i32, i32* %ptr, !xsan.mop.type !{!"Load"}
%1 = load i32, i32* %ptr, !xsan.mop.pattern !{!"Range"}
%1 = load i32, i32* %ptr, !xsan.mop.redundant !{!"false"}
%1 = load i32, i32* %ptr, !xsan.mop.full_info !{!"=== MOP Information ===\nType: Load\n..."}
%1 = load i32, i32* %ptr, !xsan.mop.dependencies !{!"MOP@0x1234", !"MOP@0x5678"}
%1 = load i32, i32* %ptr, !xsan.mop.covering !{!"MOP@0x9abc"}
```

## 查看注释

### 使用 opt 工具

```bash
# 转储 IR 查看注释
opt -S input.ll -o output.ll

# 或使用 llvm-dis
llvm-dis input.bc -o output.ll
```

### 在代码中读取 Metadata

```cpp
// 读取 MOP 类型 Metadata
if (auto* MD = Inst->getMetadata("xsan.mop.type")) {
  if (MD->getNumOperands() > 0) {
    if (auto* TypeMD = dyn_cast<MDString>(MD->getOperand(0))) {
      StringRef TypeStr = TypeMD->getString();
      // 处理类型字符串
    }
  }
}

// 读取完整信息 Metadata
if (auto* MD = Inst->getMetadata("xsan.mop.full_info")) {
  if (MD->getNumOperands() > 0) {
    if (auto* InfoMD = dyn_cast<MDString>(MD->getOperand(0))) {
      StringRef InfoStr = InfoMD->getString();
      // 处理信息字符串
    }
  }
}
```

## 注释内容说明

### MOP 类型
- `Load`: 读操作
- `Store`: 写操作
- `Atomic`: 原子操作
- `Memcpy`: 内存拷贝
- `Memset`: 内存设置

### 访问模式
- `Unknown`: 未知模式
- `Simple`: 简单访问（范围不变）
- `Range`: 范围访问 `[L, R)`
- `Periodic`: 周期性访问 `([L, R), AccessSize, Step)`
- `Cyclic`: 循环访问 `([L, R), Beg, End)`

### 冗余状态
- `true`: MOP 是冗余的（被其他 MOP 覆盖）
- `false`: MOP 不是冗余的

### 依赖关系
- 以 `MOP@` 开头的字符串，后跟指令地址，标识依赖的 MOP

### 覆盖关系
- 如果 MOP 是冗余的，会包含覆盖它的 MOP 的标识符

## 示例输出

### Basic 级别示例

```llvm
; Function: example
define void @example(i32* %ptr) {
entry:
  ; MOP[Load, Pattern:Range]
  %1 = load i32, i32* %ptr, !xsan.mop.type !{!"Load", !"Range", !"false"}
  
  ; MOP[Store, REDUNDANT, Pattern:Simple]
  store i32 %1, i32* %ptr, !xsan.mop.type !{!"Store", !"Simple", !"true"}
  
  ret void
}
```

### Full 级别示例

```llvm
; Function: example
define void @example(i32* %ptr) {
entry:
  %1 = load i32, i32* %ptr, 
    !xsan.mop.type !{!"Load"},
    !xsan.mop.pattern !{!"Range"},
    !xsan.mop.redundant !{!"false"},
    !xsan.mop.full_info !{!"=== MOP Information ===\nType: Load\nRead/Write: Read\n..."}
  
  store i32 %1, i32* %ptr,
    !xsan.mop.type !{!"Store"},
    !xsan.mop.pattern !{!"Simple"},
    !xsan.mop.redundant !{!"true"},
    !xsan.mop.covering !{!"MOP@0x1234"},
    !xsan.mop.full_info !{!"=== MOP Information ===\nType: Store\nRead/Write: Write\nRedundant: Yes\n..."}
  
  ret void
}
```

## 注意事项

1. **性能影响**：添加注释不会影响代码生成性能，但会增加 IR 文件大小
2. **Metadata 持久化**：Metadata 会保留在 IR 中，直到被显式移除
3. **调试用途**：注释主要用于调试和分析，不应依赖其存在性
4. **版本兼容性**：Metadata 格式可能在将来版本中变化

## 最佳实践

1. **开发阶段**：使用 `Full` 级别获取完整信息
2. **生产环境**：使用 `Minimal` 或 `Basic` 级别减少 IR 大小
3. **调试特定问题**：针对特定 MOP 使用 `annotateMop()` 方法
4. **自动化分析**：使用 Metadata 方式便于程序化读取和分析

## 扩展

如果需要添加自定义注释信息，可以：

1. 扩展 `MopIRAnnotator` 类
2. 添加新的 Metadata 键名
3. 实现自定义的注释生成逻辑
