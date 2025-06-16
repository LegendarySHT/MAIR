#pragma once

#include "ValueUtils.h"

// Provide some utilities for UBSan's instrumentation.
namespace llvm {
class Instruction;
}

namespace __xsan {

void markAsUbsanInst(llvm::Instruction &I);
bool isUbsanInst(const llvm::Instruction &I);

/// E.g., __ubsan_handle_XXXXX
bool isUbsanInterfaceCall(const llvm::Instruction &CB);

/// Check if the call is a checked integer overflow intrinsic call.
/// E.g, llvm.sadd.with.overflow.i32, llvm.uadd.with.overflow.i32, etc.
/// UBSan/Integer Sanitizer will instrument these calls.
bool isCheckedIntegerOverflowIntrinsicCall(llvm::Instruction &I);

/// Check if the block is a UBSan fallback block, i.e.,
/// a block contains a call to __ubsan_handle_XXXXX guarded by a UBSan check
bool isUbsanFallbackBlock(const llvm::BasicBlock &BB);
// E.g., __ubsan_XXXXX
bool isUbsanFunction(const llvm::Function &F);
// E.g., __ubsan_handle_XXXXX
bool isUbsanReportFunction(const llvm::Function &F);
} // namespace __xsan
