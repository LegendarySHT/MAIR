#include "../include/Mop.h"
#include "llvm/Support/Format.h"

using namespace __xsan::MopIR;
using namespace llvm;

// MemoryLocation 方法实现

// 判断两个内存位置是否重叠
bool MemoryLocation::overlapsWith(const MemoryLocation& Other) const {
  // 如果基地址不同，则无法确定是否重叠
  if (BasePtr != Other.BasePtr) {
    return false;
  }

  // 计算内存范围
  uint64_t ThisStart = Offset.getZExtValue();
  uint64_t ThisEnd = ThisStart + Size;
  uint64_t OtherStart = Other.Offset.getZExtValue();
  uint64_t OtherEnd = OtherStart + Other.Size;

  // 检查是否重叠
  return !(ThisEnd <= OtherStart || OtherEnd <= ThisStart);
}

// 判断是否与另一个位置相邻且可合并
bool MemoryLocation::isContiguousWith(const MemoryLocation& Other) const {
  // 如果基地址不同，则无法确定是否相邻
  if (BasePtr != Other.BasePtr) {
    return false;
  }

  // 计算内存范围
  uint64_t ThisStart = Offset.getZExtValue();
  uint64_t ThisEnd = ThisStart + Size;
  uint64_t OtherStart = Other.Offset.getZExtValue();
  uint64_t OtherEnd = OtherStart + Other.Size;

  // 检查是否相邻（一个的结束是另一个的开始）
  return (ThisEnd == OtherStart) || (OtherEnd == ThisStart);
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
  return Location.isContiguousWith(Other->Location);
}

// 合并两个MOP
std::unique_ptr<Mop> Mop::mergeWith(const Mop* Other) const {
  // 确保可以合并
  if (!canMergeWith(Other)) {
    return nullptr;
  }

  // 确定合并后的内存范围
  uint64_t ThisStart = Location.Offset.getZExtValue();
  uint64_t ThisEnd = ThisStart + Location.Size;
  uint64_t OtherStart = Other->Location.Offset.getZExtValue();
  uint64_t OtherEnd = OtherStart + Other->Location.Size;

  // 计算合并后的起始位置和大小
  uint64_t MergedStart = std::min(ThisStart, OtherStart);
  uint64_t MergedSize = std::max(ThisEnd, OtherEnd) - MergedStart;
  APInt MergedOffset(Location.Offset.getBitWidth(), MergedStart);

  // 使用最小的对齐要求
  unsigned MergedAlign = std::min(Location.Alignment, Other->Location.Alignment);

  // 创建新的MemoryLocation
  MemoryLocation MergedLoc(Location.BasePtr, MergedOffset, MergedSize, MergedAlign);

  // 创建新的Mop（使用当前Mop的指令作为原始指令）
  auto MergedMop = std::make_unique<Mop>(Type, MergedLoc, OriginalInst);

  // 复制依赖关系
  for (auto* dep : Dependencies) {
    MergedMop->addDependency(dep);
  }
  for (auto* dep : Other->Dependencies) {
    MergedMop->addDependency(dep);
  }

  return MergedMop;
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

  // 打印内存位置信息
  OS << "MemoryLocation: ";
  OS << "BasePtr: " << *Location.BasePtr << ", "
     << "Offset: 0x" << format("%llx", Location.Offset.getZExtValue()) << ", "
     << "Size: " << Location.Size << " bytes, "
     << "Alignment: " << Location.Alignment << "\n";

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