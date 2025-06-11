#pragma once

#include "ValueUtils.h"

// Provide some utilities for UBSan's instrumentation.
namespace llvm {
class Instruction;
}

namespace __xsan {

/// E.g., __ubsan_handle_XXXXX
bool isUbsanInterfaceCall(const llvm::Instruction &CB);

/// Check if the call is a checked integer overflow intrinsic call.
/// E.g, llvm.sadd.with.overflow.i32, llvm.uadd.with.overflow.i32, etc.
/// UBSan/Integer Sanitizer will instrument these calls.
/// @param strict: if true, track the use-def chain of the branch instruction.
bool isCheckedIntegerOverflowIntrinsicCall(llvm::Instruction &I, bool strict = false);

/// Check if the block is a UBSan fallback block, i.e.,
/// a block contains a call to __ubsan_handle_XXXXX guarded by a UBSan check
bool isUbsanFallbackBlock(const llvm::BasicBlock &BB);
} // namespace __xsan
