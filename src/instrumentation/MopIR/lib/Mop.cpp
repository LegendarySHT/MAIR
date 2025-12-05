#include "../include/Mop.h"
#include "llvm/Support/Format.h"

using namespace __xsan::MopIR;
using namespace llvm;

// 辅助函数：判断两个内存位置是否相邻且可合并
static bool isContiguousWith(const MemoryLocation& Loc1, const MemoryLocation& Loc2) {
  // 如果基地址不同，则无法确定是否相邻
  if (Loc1.Ptr != Loc2.Ptr) {
    return false;
  }

  // 计算内存范围（简化处理，假设偏移量可以通过其他方式获取）
  // 注意：LLVM的MemoryLocation没有直接的偏移量信息，需要从其他途径获取
  // 这里我们暂时返回false，需要重新设计合并逻辑
  return false;
}

// 辅助函数：判断两个内存位置是否重叠
static bool overlapsWith(const MemoryLocation& Loc1, const MemoryLocation& Loc2) {
  // 使用LLVM提供的重叠检查功能
  // 如果基地址不同，则无法确定是否重叠
  if (Loc1.Ptr != Loc2.Ptr) {
    return false;
  }
  
  // 简化处理：如果大小未知，保守返回true
  if (!Loc1.Size.hasValue() || !Loc2.Size.hasValue()) {
    return true;
  }
  
  // 计算内存范围（简化处理，假设偏移量可以通过其他方式获取）
  // 这里我们暂时返回true，需要重新设计重叠检查逻辑
  return true;
}

// Mop 方法实现

// 判断是否可以与其他MOP合并
bool Mop::canMergeWith(const Mop* Other) const {
  // 只有相同类型的操作才能合并
  if (Type != Other->Type) {
    return false;
  }

  // 原子操作不支持合并
  if (Type == MopType::Atomic) {
    return false;
  }

  // 检查内存位置是否相邻且可合并
  return isContiguousWith(Location, Other->Location);
}

// 合并两个MOP
std::unique_ptr<Mop> Mop::mergeWith(const Mop* Other) const {
  // 确保可以合并
  if (!canMergeWith(Other)) {
    return nullptr;
  }

  // 由于LLVM的MemoryLocation没有直接的偏移量信息，合并逻辑需要重新设计
  // 这里我们暂时返回nullptr，表示不支持合并
  // 在实际实现中，需要从其他途径获取偏移量信息，或者重新设计Mop的数据结构
  return nullptr;
}

// 打印MOP信息
void Mop::print(raw_ostream& OS) const {
  // 打印操作类型
  OS << "MopType: ";
  switch (Type) {
    case MopType::Load:
      OS << "Load";
      break;
    case MopType::Store:
      OS << "Store";
      break;
    case MopType::Atomic:
      OS << "Atomic";
      break;
    case MopType::Memcpy:
      OS << "Memcpy";
      break;
    case MopType::Memset:
      OS << "Memset";
      break;
  }
  OS << "\n";

  // 打印内存位置信息（适配LLVM的MemoryLocation）
  OS << "MemoryLocation: ";
  OS << "Ptr: " << *Location.Ptr;
  if (Location.Size.hasValue()) {
    OS << ", Size: " << Location.Size.getValue() << " bytes";
  } else {
    OS << ", Size: unknown";
  }
  if (Location.AATags) {
    OS << ", AATags: present";
  }
  OS << "\n";

  // 打印原始指令
  OS << "Original Instruction: ";
  if (OriginalInst) {
    OS << *OriginalInst;
  } else {
    OS << "<null>";
  }
  OS << "\n";

  // 打印依赖关系
  OS << "Dependencies: [";
  for (size_t i = 0; i < Dependencies.size(); ++i) {
    if (i > 0) {
      OS << ", ";
    }
    OS << "Mop@" << format("%p", Dependencies[i]);
  }
  OS << "]\n";
}