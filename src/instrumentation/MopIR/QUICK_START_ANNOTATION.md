# MopIR 注释功能快速开始

## 最简单的使用方式

```cpp
#include "MopIR.h"

// 在 Pass 中
void MyPass::runOnFunction(Function& F, FunctionAnalysisManager& FAM) {
  // 创建 MopIR
  MopIR MopIR(F, FAM);
  
  // 构建、分析和优化
  MopIR.buildAnalyzeAndOptimize();
  
  // 添加注释（一行代码搞定！）
  MopIR.annotateIR();
  
  // 现在 IR 中已经包含了 MopIR 的注释信息
}
```

## 查看注释

### 方法 1: 使用 opt 转储 IR

```bash
opt -S input.ll -o output.ll
cat output.ll | grep "xsan.mop"
```

### 方法 2: 在代码中读取

```cpp
// 检查指令是否有 MopIR 注释
if (auto* MD = Inst->getMetadata("xsan.mop.type")) {
  // 有注释，可以读取信息
  errs() << "This instruction has MopIR annotation\n";
}
```

## 自定义注释级别

```cpp
// 最小信息（仅类型和冗余状态）
MopIR.annotateIR(AnnotationLevel::Minimal);

// 基本信息（默认，推荐）
MopIR.annotateIR(AnnotationLevel::Basic);

// 详细信息
MopIR.annotateIR(AnnotationLevel::Detailed);

// 完整信息（包含所有细节）
MopIR.annotateIR(AnnotationLevel::Full);
```

## 示例输出

添加注释后，IR 会变成这样：

```llvm
define void @example(i32* %ptr) {
entry:
  ; 原始 IR
  %1 = load i32, i32* %ptr
  
  ; 添加注释后
  %1 = load i32, i32* %ptr, !xsan.mop.type !{!"Load", !"Range", !"false"}
  
  ret void
}
```

## 常见问题

### Q: 注释会影响代码生成吗？
A: 不会。Metadata 不会影响代码生成，只用于调试和分析。

### Q: 如何移除注释？
A: Metadata 会一直保留，但可以通过 Pass 移除：
```cpp
Inst->setMetadata("xsan.mop.type", nullptr);
```

### Q: 注释会增加 IR 大小吗？
A: 会，但通常影响很小。使用 `Minimal` 级别可以最小化影响。

### Q: 可以在生产代码中使用吗？
A: 可以，但建议使用 `Minimal` 或 `Basic` 级别以减少 IR 大小。

## 更多信息

- 详细文档：`ANNOTATION_GUIDE.md`
- 实现总结：`ANNOTATION_SUMMARY.md`
