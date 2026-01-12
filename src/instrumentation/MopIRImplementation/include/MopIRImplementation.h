#ifndef MOP_IR_IMPLEMENTATION_H
#define MOP_IR_IMPLEMENTATION_H

/**
 * MopIRImplementation - 独立的 Pass Manager 实现
 * 
 * 这是一个轻量级的、不依赖大型框架的 Pass Manager 实现。
 * 主要用于管理 IR 优化 Pass 的执行。
 * 
 * 主要组件：
 * - Pass: Pass 基类接口
 * - PassManager: Pass 管理器
 * - PassContext: Pass 执行上下文（分析结果缓存）
 * - IRUnit: IR 单元基类
 */

// 核心组件
#include "Pass.h"
#include "PassManager.h"
#include "PassContext.h"
#include "IRUnit.h"

// 示例（可选）
// #include "ExamplePass.h"

#endif // MOP_IR_IMPLEMENTATION_H

