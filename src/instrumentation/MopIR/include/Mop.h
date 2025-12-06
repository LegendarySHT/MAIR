#ifndef XSAN_MOP_IR_MOP_H
#define XSAN_MOP_IR_MOP_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
class Loop;  // 前向声明
}

namespace __xsan {
namespace MopIR {

// 内存操作类型枚举
enum class MopType {
  Load,      // 读操作
  Store,     // 写操作
  Atomic,    // 原子操作
  Memcpy,    // 内存拷贝
  Memset     // 内存设置
};

// 访问模式枚举
enum class AccessPattern {
  Unknown,        // 未知模式（非循环访问或无法分析）
  Simple,         // 简单访问（范围不变的内存访问）
  Range,          // 范围访问: 访问 [L, R), 步长等于访问大小
  Periodic,       // 周期性访问: 访问 ([L, R), AccessSize, Step), 按步长分割范围
  Cyclic          // 循环访问: 访问 ([L, R), Beg, End), 访问 [Beg, R) 和 [L, End)
};

// 访问模式信息结构
struct AccessPatternInfo {
  AccessPattern Pattern;           // 访问模式类型
  
  // 范围访问 (Range Access): [L, R)
  llvm::Value* RangeBegin;         // L - 范围起始地址
  llvm::Value* RangeEnd;           // R - 范围结束地址
  
  // 周期性访问 (Periodic Access): ([L, R), AccessSize, Step)
  llvm::Value* PeriodicBegin;      // L - 周期起始地址
  llvm::Value* PeriodicEnd;         // R - 周期结束地址
  uint64_t AccessSize;             // AccessSize - 每次访问的大小
  llvm::Value* Step;               // Step - 步长值（可能为常量或值）
  bool StepIsConstant;             // Step 是否为常量
  uint64_t StepConstant;           // Step 的常量值（如果 StepIsConstant 为 true）
  
  // 循环访问 (Cyclic Access): ([L, R), Beg, End)
  llvm::Value* CyclicBegin;         // L - 循环范围起始
  llvm::Value* CyclicEnd;           // R - 循环范围结束
  llvm::Value* CyclicAccessBeg;     // Beg - 第一个访问段起始
  llvm::Value* CyclicAccessEnd;     // End - 第二个访问段结束
  
  // 关联的循环信息
  llvm::Loop* AssociatedLoop;       // 关联的循环（如果存在）
  
  // 访问大小（所有模式通用）
  uint64_t MopSize;                 // MOP 的访问大小（字节）
  
  AccessPatternInfo()
    : Pattern(AccessPattern::Unknown),
      RangeBegin(nullptr), RangeEnd(nullptr),
      PeriodicBegin(nullptr), PeriodicEnd(nullptr),
      AccessSize(0), Step(nullptr),
      StepIsConstant(false), StepConstant(0),
      CyclicBegin(nullptr), CyclicEnd(nullptr),
      CyclicAccessBeg(nullptr), CyclicAccessEnd(nullptr),
      AssociatedLoop(nullptr), MopSize(0) {}
  
  // 检查是否为范围访问
  bool isRangeAccess() const { return Pattern == AccessPattern::Range; }
  
  // 检查是否为周期性访问
  bool isPeriodicAccess() const { return Pattern == AccessPattern::Periodic; }
  
  // 检查是否为循环访问
  bool isCyclicAccess() const { return Pattern == AccessPattern::Cyclic; }
  
  // 检查是否为简单访问（范围不变）
  bool isSimpleAccess() const { return Pattern == AccessPattern::Simple; }
  
  // 检查是否有已知模式
  bool hasKnownPattern() const { return Pattern != AccessPattern::Unknown; }
};

// MOP基类
class Mop {
private:
  MopType Type;                           // 操作类型
  llvm::MemoryLocation Location;          // 内存位置（使用LLVM官方实现）
  llvm::Instruction* OriginalInst;        // 原始LLVM指令
  llvm::SmallVector<Mop*, 4> Dependencies; // 依赖的其他MOP
  bool IsRedundant;                       // 标记是否为冗余MOP（用于优化）
  Mop* CoveringMop;                       // 覆盖此MOP的另一个MOP（如果冗余）
  AccessPatternInfo PatternInfo;          // 访问模式信息（用于插桩优化）

public:
  Mop(MopType Ty, const llvm::MemoryLocation& Loc, llvm::Instruction* Inst)
    : Type(Ty), Location(Loc), OriginalInst(Inst), 
      IsRedundant(false), CoveringMop(nullptr), PatternInfo() {}
  
  // 访问器方法
  MopType getType() const { return Type; }
  const llvm::MemoryLocation& getLocation() const { return Location; }
  llvm::Instruction* getOriginalInst() const { return OriginalInst; }
  
  // 判断是否为写操作
  bool isWrite() const {
    return Type == MopType::Store || Type == MopType::Memset || 
           Type == MopType::Memcpy;
  }
  
  // 判断是否为读操作
  bool isRead() const {
    return Type == MopType::Load;
  }
  
  // 冗余标记相关方法
  bool isRedundant() const { return IsRedundant; }
  void setRedundant(bool Redundant) { IsRedundant = Redundant; }
  Mop* getCoveringMop() const { return CoveringMop; }
  void setCoveringMop(Mop* Covering) { CoveringMop = Covering; }
  
  // 访问模式相关方法
  const AccessPatternInfo& getAccessPattern() const { return PatternInfo; }
  AccessPatternInfo& getAccessPattern() { return PatternInfo; }
  void setAccessPattern(const AccessPatternInfo& Pattern) { PatternInfo = Pattern; }
  
  // 便捷方法：检查访问模式
  bool hasKnownAccessPattern() const { return PatternInfo.hasKnownPattern(); }
  bool isRangeAccess() const { return PatternInfo.isRangeAccess(); }
  bool isPeriodicAccess() const { return PatternInfo.isPeriodicAccess(); }
  bool isCyclicAccess() const { return PatternInfo.isCyclicAccess(); }
  bool isSimpleAccess() const { return PatternInfo.isSimpleAccess(); }
  
  // 判断是否可以与其他MOP合并
  bool canMergeWith(const Mop* Other) const;
  
  // 合并两个MOP
  std::unique_ptr<Mop> mergeWith(const Mop* Other) const;
  
  // 添加依赖关系
  void addDependency(Mop* Dep) { Dependencies.push_back(Dep); }
  
  // 获取依赖列表
  const llvm::SmallVectorImpl<Mop*>& getDependencies() const { return Dependencies; }
  
  // 打印MOP信息
  void print(llvm::raw_ostream& OS) const;
};

// MOP集合
using MopList = llvm::SmallVector<std::unique_ptr<Mop>, 16>;

} // namespace MopIR
} // namespace __xsan

#endif // XSAN_MOP_IR_MOP_H