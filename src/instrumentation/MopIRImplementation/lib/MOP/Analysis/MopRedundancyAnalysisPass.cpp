#include "MOP/Analysis/MopRedundancyAnalysisPass.h"
#include "MOP/MOPData.h"
#include "MOP/MOPIRUnit.h"
#include "LLVM/LLVMPassContext.h"

#ifdef MOP_IR_USE_LLVM
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Debug.h"
// 使用独立的 ActiveMopAnalysis 实现（不依赖原项目源代码）
#include "MOP/Analysis/ActiveMopAnalysisAdapter.h"
#include <unordered_set>
#include <iostream>
#endif

namespace MopIRImpl {
namespace MOP {

#ifdef MOP_IR_USE_LLVM

const llvm::Instruction* MopRedundancyChecker::isCovering(Mop* Mop1, Mop* Mop2, bool WriteSensitive) {
    if (!Mop1 || !Mop2 || Mop1 == Mop2) {
      return nullptr;
    }
    
    const llvm::Instruction* Inst1 = Mop1->getInstruction();
    const llvm::Instruction* Inst2 = Mop2->getInstruction();
    
    if (!Inst1 || !Inst2) {
      return nullptr;
    }
    
    std::cout << "\n[DEBUG] 检查覆盖关系:\n";
    std::cout << "  MOP1: ";
    Inst1->print(llvm::outs());
    std::cout << "\n  MOP2: ";
    Inst2->print(llvm::outs());
    std::cout << "\n";
    
    // 条件 4：写操作条件（可选）
    // 原项目逻辑：isWrite1 || (isWrite1 == isWrite2)
    // 等价于：!(isWrite1 == false && isWrite1 != isWrite2)
    // 即：如果 KillingMop 不是写操作，且 KillingMop 和 DeadMop 的写操作类型不同，则不覆盖
    if (WriteSensitive) {
      if (!Mop1->isWrite() && Mop1->isWrite() != Mop2->isWrite()) {
        std::cout << "  [DEBUG] 写操作条件不满足: Mop1->isWrite()=" << Mop1->isWrite() 
                  << ", Mop2->isWrite()=" << Mop2->isWrite() << "\n";
        return nullptr;
      }
      std::cout << "  [DEBUG] 写操作条件满足: Mop1->isWrite()=" << Mop1->isWrite() 
                << ", Mop2->isWrite()=" << Mop2->isWrite() << "\n";
    }
    
    // 条件 2：支配关系
    const llvm::Instruction* FromI = nullptr;
    bool hasDom = getDT().dominates(Inst1, Inst2);
    bool hasPDom = getPDT().dominates(Inst1, Inst2);
    
    std::cout << "  [DEBUG] 支配关系检查:\n";
    std::cout << "    Inst1 dom Inst2: " << (hasDom ? "是" : "否") << "\n";
    std::cout << "    Inst1 pdom Inst2: " << (hasPDom ? "是" : "否") << "\n";
    
    if (hasDom) {
      FromI = Inst1;
    } else if (hasPDom) {
      FromI = Inst2;
    } else {
      std::cout << "  [DEBUG] 没有支配关系，返回 nullptr\n";
      return nullptr;  // 没有支配关系
    }
    
    // 条件 1：内存范围包含
    const llvm::MemoryLocation& Loc1 = Mop1->getMemoryLocation();
    const llvm::MemoryLocation& Loc2 = Mop2->getMemoryLocation();
    
    bool rangeContains = isAccessRangeContains(Inst1, Inst2, Loc1, Loc2);
    std::cout << "  [DEBUG] 内存范围包含检查: " << (rangeContains ? "是" : "否") << "\n";
    
    if (!rangeContains) {
      std::cout << "  [DEBUG] 内存范围不包含，返回 nullptr\n";
      return nullptr;  // 内存范围不包含
    }
    
    // 注意：不检查 ActiveMopAnalysis，该检查在 distillRecurringChecks 中进行
    std::cout << "  [DEBUG] 所有条件满足，MOP1 覆盖 MOP2，返回 FromI\n";
    return FromI;
  }
  
  /**
   * 检查内存范围包含关系
   * 
   * 参考原项目的实现，使用 GetPointerBaseWithConstantOffset
   */
  bool MopRedundancyChecker::isAccessRangeContains(const llvm::Instruction* KillingI,
                            const llvm::Instruction* DeadI,
                            const llvm::MemoryLocation& KillingLoc,
                            const llvm::MemoryLocation& DeadLoc) {
    std::cout << "    [DEBUG] isAccessRangeContains 开始检查\n";
    
    // 检查大小是否已知
    if (!KillingLoc.Size.isPrecise() || !DeadLoc.Size.isPrecise()) {
      std::cout << "    [DEBUG] 大小不精确，返回 false\n";
      return false;
    }
    
    uint64_t KillingSize = KillingLoc.Size.getValue();
    uint64_t DeadSize = DeadLoc.Size.getValue();
    
    std::cout << "    [DEBUG] KillingSize: " << KillingSize 
              << ", DeadSize: " << DeadSize << "\n";
    
    // 使用别名分析
    llvm::AliasResult AAR = getAA().alias(KillingLoc, DeadLoc);
    
    std::cout << "    [DEBUG] 别名分析结果: ";
    switch (AAR) {
      case llvm::AliasResult::NoAlias:
        std::cout << "NoAlias";
        break;
      case llvm::AliasResult::MayAlias:
        std::cout << "MayAlias";
        break;
      case llvm::AliasResult::PartialAlias:
        std::cout << "PartialAlias";
        if (AAR.hasOffset()) {
          std::cout << " (offset: " << AAR.getOffset() << ")";
        }
        break;
      case llvm::AliasResult::MustAlias:
        std::cout << "MustAlias";
        break;
    }
    std::cout << "\n";
    
    // 如果两个位置完全相同（MustAlias）
    if (AAR == llvm::AliasResult::MustAlias) {
      bool result = KillingSize >= DeadSize;
      std::cout << "    [DEBUG] MustAlias，大小比较: " << KillingSize 
                << " >= " << DeadSize << " = " << (result ? "true" : "false") << "\n";
      return result;
    }
    
    // 如果部分别名且有偏移信息
    if (AAR == llvm::AliasResult::PartialAlias && AAR.hasOffset()) {
      int32_t Off = AAR.getOffset();
      std::cout << "    [DEBUG] PartialAlias，偏移: " << Off << "\n";
      // 检查 Dead 是否完全在 Killing 范围内
      if (Off >= 0 && uint64_t(Off) + DeadSize <= KillingSize) {
        std::cout << "    [DEBUG] PartialAlias 检查通过，返回 true\n";
        return true;
      }
    }
    
    // 使用 GetPointerBaseWithConstantOffset 获取基指针和偏移
    int64_t KillingOff = 0, DeadOff = 0;
    const llvm::Value* KillingPtr = KillingLoc.Ptr->stripPointerCasts();
    const llvm::Value* DeadPtr = DeadLoc.Ptr->stripPointerCasts();
    
    std::cout << "    [DEBUG] KillingPtr: ";
    KillingPtr->print(llvm::outs());
    std::cout << "\n    [DEBUG] DeadPtr: ";
    DeadPtr->print(llvm::outs());
    std::cout << "\n";
    
    const llvm::Value* KillingBasePtr = 
        llvm::GetPointerBaseWithConstantOffset(KillingPtr, KillingOff, getDL());
    const llvm::Value* DeadBasePtr = 
        llvm::GetPointerBaseWithConstantOffset(DeadPtr, DeadOff, getDL());
    
    std::cout << "    [DEBUG] KillingBasePtr: ";
    KillingBasePtr->print(llvm::outs());
    std::cout << ", KillingOff: " << KillingOff << "\n";
    std::cout << "    [DEBUG] DeadBasePtr: ";
    DeadBasePtr->print(llvm::outs());
    std::cout << ", DeadOff: " << DeadOff << "\n";
    
    // 如果基指针不同，不是覆盖关系
    if (KillingBasePtr != DeadBasePtr) {
      std::cout << "    [DEBUG] 基指针不同，返回 false\n";
      return false;
    }
    
    // 检查 Dead 是否完全在 Killing 范围内
    // 注意：这里需要检查 Killing 是否覆盖 Dead，所以：
    // - Killing 的起始位置 <= Dead 的起始位置
    // - Killing 的结束位置 >= Dead 的结束位置
    // 或者等价地：
    // - Dead 的起始位置 >= Killing 的起始位置
    // - Dead 的结束位置 <= Killing 的结束位置
    if (DeadOff >= KillingOff) {
      // Dead 的起始位置在 Killing 之后或相同
      // 检查 Dead 的结束位置是否在 Killing 范围内
      uint64_t offsetDiff = DeadOff - KillingOff;
      uint64_t deadEndInKilling = offsetDiff + DeadSize;
      bool result = deadEndInKilling <= KillingSize;
      
      std::cout << "    [DEBUG] DeadOff >= KillingOff\n";
      std::cout << "    [DEBUG] offsetDiff: " << offsetDiff 
                << ", deadEndInKilling: " << deadEndInKilling 
                << ", KillingSize: " << KillingSize 
                << ", 结果: " << (result ? "true" : "false") << "\n";
      
      if (result) {
        return true;
      }
    } else {
      // Dead 的起始位置在 Killing 之前
      std::cout << "    [DEBUG] DeadOff < KillingOff，检查 Killing 是否覆盖 Dead\n";
      uint64_t offsetDiff = KillingOff - DeadOff;
      if (offsetDiff <= DeadSize && 
          DeadSize <= KillingSize + offsetDiff) {
        // 这种情况比较复杂，需要更仔细的分析
        // 简化处理：如果 Killing 的大小足够大，且偏移差在合理范围内
        if (KillingSize >= DeadSize + offsetDiff) {
          std::cout << "    [DEBUG] Killing 覆盖 Dead，返回 true\n";
          return true;
        }
      }
    }
    
    std::cout << "    [DEBUG] 所有检查都失败，返回 false\n";
    return false;
  }

#endif // MOP_IR_USE_LLVM

const llvm::Instruction* MopRedundancyAnalysisPass::isCovering(Mop* Mop1, Mop* Mop2, 
                                                                 MopIRImpl::PassContext& Context) {
#ifdef MOP_IR_USE_LLVM
  if (Context.getPassContextKind() != MopIRImpl::PassContext::Kind::LLVM) {
    if (!Mop1 || !Mop2 || Mop1 == Mop2) {
      return nullptr;
    }

    if (Mop1->getType() == Mop2->getType() &&
        Mop1->getSize() >= Mop2->getSize() &&
        Mop1->getPointer() == Mop2->getPointer()) {
      return Mop1->getInstruction();
    }

    return nullptr;
  }

  auto& llvmContext = static_cast<LLVM::LLVMPassContext&>(Context);

  const llvm::Instruction* Inst1 = Mop1->getInstruction();
  const llvm::Instruction* Inst2 = Mop2->getInstruction();

  if (!Inst1 || !Inst2) {
    return nullptr;
  }

  llvm::Function* Func = const_cast<llvm::Function*>(Inst1->getFunction());
  if (!Func || Func != Inst2->getFunction()) {
    return nullptr;
  }

  llvmContext.setCurrentFunction(*Func);

  MopRedundancyChecker Checker(*Func, llvmContext);
  
  // 检查是否覆盖（不检查 ActiveMopAnalysis，该检查在 distillRecurringChecks 中进行）
  // 返回 FromI（Instruction*）如果覆盖，否则返回 nullptr
  return Checker.isCovering(Mop1, Mop2, false);
#else
  // 非 LLVM 版本：简化实现
  if (!Mop1 || !Mop2 || Mop1 == Mop2) {
    return nullptr;
  }
  
  // 检查基本条件
  if (Mop1->getType() == Mop2->getType() && 
      Mop1->getSize() >= Mop2->getSize() &&
      Mop1->getPointer() == Mop2->getPointer()) {
    // 非 LLVM 版本无法确定 FromI，返回 Mop1 的指令
    return Mop1->getInstruction();
  }
  
  return nullptr;
#endif
}

} // namespace MOP
} // namespace MopIRImpl

