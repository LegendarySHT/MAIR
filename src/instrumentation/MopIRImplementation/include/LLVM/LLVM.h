#ifndef MOP_IR_IMPLEMENTATION_LLVM_H
#define MOP_IR_IMPLEMENTATION_LLVM_H

/**
 * LLVM 适配层主头文件
 * 
 * 包含所有 LLVM 相关的组件
 * 只有在定义了 MOP_IR_USE_LLVM 宏时才会编译
 */

#ifdef MOP_IR_USE_LLVM

#include "LLVM/LLVMIRUnit.h"
#include "LLVM/LLVMPassContext.h"
#include "LLVM/LLVMPass.h"
#include "LLVM/LLVMPassManager.h"

namespace MopIRImpl {
namespace LLVM {

// 导出常用类型别名
using FunctionPass = LLVM::FunctionPass;
using ModulePass = LLVM::ModulePass;
using LLVMPassContext = LLVM::LLVMPassContext;
using LLVMPassManager = LLVM::LLVMPassManager;
using FunctionIRUnit = LLVM::FunctionIRUnit;
using ModuleIRUnit = LLVM::ModuleIRUnit;

} // namespace LLVM
} // namespace MopIRImpl

#endif // MOP_IR_USE_LLVM

#endif // MOP_IR_IMPLEMENTATION_LLVM_H

