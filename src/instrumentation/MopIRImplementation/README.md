# MopIRImplementation

一个独立的、轻量级的 Pass Manager 实现，用于管理 IR 优化 Pass 的执行。

## 设计目标

- **独立性**：不依赖于大型框架（如 LLVM），可以独立使用
- **可选 LLVM 支持**：可以选择性地启用 LLVM 支持，获得更好的分析能力
- **可扩展性**：方便添加新的优化 Pass
- **类型安全**：使用 C++ 模板和类型系统保证类型安全
- **简洁性**：接口简单清晰，易于理解和使用

## 两种使用模式

### 1. 基础模式（不依赖 LLVM）

完全独立，只使用 C++ 标准库，适合：
- 独立 IR 优化工具
- 教学和学习
- 原型开发
- 轻量级工具

### 2. LLVM 模式（可选）

启用 LLVM 支持后，可以获得：
- 自动集成 LLVM 分析（DominatorTree、LoopInfo、ScalarEvolution 等）
- 智能的分析结果缓存和失效
- 与现有 LLVM Pass 无缝集成
- 类型安全的 LLVM IR 包装

详见 [LLVM 支持文档](explanation/README_LLVM.md)

## 核心组件

### 1. Pass 基类 (`Pass.h`)

所有优化 Pass 都应该继承自 `Pass` 基类：

```cpp
class MyPass : public Pass {
public:
  bool optimize(IRUnit& IR, PassContext& Context) override {
    // 实现优化逻辑
    return modified; // 返回是否修改了 IR
  }
  
  const char* getName() const override {
    return "MyPass";
  }
};
```

### 2. PassManager (`PassManager.h`)

管理 Pass 的执行顺序和生命周期：

```cpp
PassManager PM;
PM.addPass(std::make_unique<MyPass>());
PM.run(IR, &Context);
```

### 3. PassContext (`PassContext.h`)

在 Pass 之间传递分析结果和共享数据：

```cpp
// 获取或计算分析结果
auto& analysis = Context.getOrCompute<MyAnalysis>([]() {
  return MyAnalysis();
});

// 获取已存在的分析结果
auto* analysis = Context.get<MyAnalysis>();
```

### 4. IRUnit (`IRUnit.h`)

表示一个可以被优化的 IR 单元（函数、模块等）：

```cpp
class MyIRUnit : public IRUnit {
public:
  std::string getName() const override {
    return "my_function";
  }
};
```

## 使用示例

### 基本使用

```cpp
#include "PassManager.h"
#include "Pass.h"

// 1. 创建 Pass Manager
PassManager PM;

// 2. 添加 Pass
PM.addPass(std::make_unique<MyPass1>());
PM.addPass(std::make_unique<MyPass2>());

// 3. 创建 IR 单元
MyIRUnit IR("function_name");

// 4. 运行优化
PassContext Context;
bool modified = PM.run(IR, &Context);
```

### 带统计信息的运行

```cpp
auto stats = PM.runWithStats(IR, &Context);
std::cout << "Passes run: " << stats.PassesRun << std::endl;
std::cout << "Passes modified: " << stats.PassesModified << std::endl;
```

### 使用分析结果缓存

```cpp
class MyAnalysis {
public:
  int getResult() const { return 42; }
};

class MyPass : public Pass {
  bool optimize(IRUnit& IR, PassContext& Context) override {
    // 获取或计算分析结果（只计算一次）
    auto& analysis = Context.getOrCompute<MyAnalysis>([]() {
      return MyAnalysis();
    });
    
    // 使用分析结果
    int result = analysis.getResult();
    
    return false;
  }
};
```

## 目录结构

```
MopIRImplementation/
├── README.md                    # 主文档（快速开始）
├── CMakeLists.txt              # CMake 构建配置
│
├── include/                     # 头文件目录
│   ├── Pass.h                  # Pass 基类接口
│   ├── PassManager.h           # Pass Manager 核心类
│   ├── PassContext.h           # Pass 上下文（分析结果缓存）
│   ├── IRUnit.h                # IR 单元基类
│   ├── ExamplePass.h           # 示例 Pass
│   ├── MopIRImplementation.h   # 主头文件（包含所有组件）
│   │
│   ├── LLVM/                   # LLVM 适配层（可选）
│   │   ├── LLVM.h              # LLVM 主头文件
│   │   ├── LLVMIRUnit.h        # LLVM Function/Module 包装器
│   │   ├── LLVMPassContext.h   # LLVM Pass 上下文
│   │   ├── LLVMPass.h          # LLVM Pass 基类
│   │   └── LLVMPassManager.h   # LLVM Pass Manager
│   │
│   └── MOP/                    # MOP 高阶 IR 实现
│       ├── MOPAll.h            # MOP 主头文件
│       ├── MOPData.h           # MOP 数据结构定义
│       ├── MOPIRUnit.h         # MOP IRUnit 包装器
│       │
│       ├── Analysis/            # MOP 分析 Pass
│       │   └── MopRedundancyAnalysisPass.h  # 冗余关系分析
│       │
│       └── Passes/              # MOP 转换 Pass
│           ├── RedundantCheckEliminationPass.h    # 冗余检查消除
│           └── ContiguousMopMergerPass.h          # 连续 MOP 合并
│
├── lib/                        # 实现文件目录（目前为空）
│   └── MOP/                    # MOP 实现文件（待实现）
│
├── examples/                   # 示例代码
│   ├── simple_example.cpp      # 基础 Pass Manager 使用示例
│   ├── llvm_example.cpp        # LLVM Pass Manager 使用示例
│   ├── mop_pass_ordering_example.cpp  # Pass 顺序示例
│   └── pass_ordering_guide.md  # Pass 顺序指南
│
├── test/                       # 测试代码
│   ├── test_framework.cpp      # 框架功能测试
│   ├── CMakeLists.txt          # 测试构建配置
│   └── README.md               # 测试说明
│
├── explanation/                # 解释性文档
│   ├── README.md               # 文档索引
│   ├── ARCHITECTURE.md         # 架构设计文档
│   ├── MIGRATION_GUIDE.md      # 迁移指南
│   ├── README_LLVM.md          # LLVM 支持文档
│   ├── MOP_PASS_GUIDE.md       # MOP Pass 编写指南
│   └── MOP_STRUCTURE.md        # MOP 文件结构说明
│
├── todo/                       # TODO 列表
│   └── MOP_IR_TODO.md          # MOP IR 实现 TODO
│
└── implDocs/                   # 实现文档
    ├── PROJECT_STRUCTURE.md    # 项目结构文档
    └── COMPONENT_STATUS.md     # 组件状态文档
```

## 扩展指南

### 添加新的 Pass

1. 继承 `Pass` 基类
2. 实现 `optimize()` 方法
3. 实现 `getName()` 方法
4. 可选：实现 `getDescription()` 和 `isRequired()`

```cpp
class MyNewPass : public Pass {
public:
  bool optimize(IRUnit& IR, PassContext& Context) override {
    // 实现优化逻辑
    return modified;
  }
  
  const char* getName() const override {
    return "MyNewPass";
  }
};
```

### 添加新的分析结果类型

分析结果类型可以是任何可拷贝的类型：

```cpp
struct MyAnalysis {
  int value;
  std::string name;
};

// 在 Pass 中使用
auto& analysis = Context.getOrCompute<MyAnalysis>([]() {
  MyAnalysis result;
  result.value = 42;
  result.name = "analysis";
  return result;
});
```

## 与 MopIR 的关系

- **MopIR**：原有的实现，保持不变
- **MopIRImplementation**：新的独立 Pass Manager 实现，不依赖 MopIR

两者可以共存，互不影响。

## LLVM 支持

如果你可以使用 LLVM，强烈建议启用 LLVM 支持以获得更好的效果：

1. **启用方式**：在 CMakeLists.txt 中设置 `option(MOP_IR_USE_LLVM ON)` 或定义 `MOP_IR_USE_LLVM` 宏
2. **优势**：自动集成 LLVM 分析、智能缓存、与现有 LLVM Pass 集成
3. **迁移**：从基础版本迁移到 LLVM 版本很简单，详见 [迁移指南](explanation/MIGRATION_GUIDE.md)

快速开始：
```cpp
#define MOP_IR_USE_LLVM
#include "LLVM/LLVM.h"

llvm::FunctionAnalysisManager& FAM = ...;
LLVMPassManager PM(&FAM);
PM.addPass(std::make_unique<MyLLVMPass>());
PM.run(F);
```

## 设计理念

1. **简单优先**：接口设计简单直观，易于理解和使用
2. **类型安全**：充分利用 C++ 类型系统，避免运行时错误
3. **可扩展**：通过继承和组合，方便扩展功能
4. **独立性强**：不依赖外部大型框架，可以独立使用

## 未来改进方向

- [ ] 添加 Pass 依赖关系管理
- [ ] 支持 Pass 的条件执行
- [ ] 添加更详细的统计和日志功能
- [ ] 支持 Pass 的并行执行（如果安全）
- [ ] 添加 Pass 的验证和测试框架

