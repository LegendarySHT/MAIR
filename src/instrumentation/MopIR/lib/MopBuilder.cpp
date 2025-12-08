#include "../include/MopBuilder.h"
#include "../../Utils/ValueUtils.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Analysis/ValueTracking.h"

using namespace __xsan::MopIR;
using namespace llvm;

namespace __xsan {
namespace MopIR {

// 地址分析，提取基地址和偏移量
std::pair<Value*, APInt> MopBuilder::analyzeAddress(Value* Ptr) const {
  // 使用LLVM提供的函数获取底层对象和偏移量
  APInt Offset(64, 0);
  Value* BaseObj = const_cast<Value*>(getUnderlyingObject(Ptr));
  return std::make_pair(BaseObj, Offset);
}

// 创建MOP对象
std::unique_ptr<Mop> MopBuilder::createMop(Instruction* Inst) const {
  // 获取数据布局信息
  const DataLayout& DL = F.getParent()->getDataLayout();
  
  // 根据指令类型创建不同的MOP
  if (LoadInst* LI = dyn_cast<LoadInst>(Inst)) {
    // 处理Load指令
    Value* Ptr = LI->getPointerOperand();
    
    // 获取访问大小
    uint64_t Size = DL.getTypeStoreSizeInBits(LI->getType()) / 8;
    
    // 使用LLVM的MemoryLocation构造函数
    MemoryLocation Loc = MemoryLocation::get(LI);
    
    // 创建MOP对象
    return std::make_unique<Mop>(MopType::Load, Loc, Inst);
  } 
  if (StoreInst* SI = dyn_cast<StoreInst>(Inst)) {
    // 处理Store指令
    Value* Ptr = SI->getPointerOperand();
    
    // 获取访问大小
    uint64_t Size = DL.getTypeStoreSizeInBits(SI->getValueOperand()->getType()) / 8;
    
    // 使用LLVM的MemoryLocation构造函数
    MemoryLocation Loc = MemoryLocation::get(SI);
    
    // 创建MOP对象
    return std::make_unique<Mop>(MopType::Store, Loc, Inst);
  }
  if (AtomicRMWInst* AI = dyn_cast<AtomicRMWInst>(Inst)) {
    // 处理原子RMW指令
    Value* Ptr = AI->getPointerOperand();
    
    // 获取访问大小
    uint64_t Size = DL.getTypeStoreSizeInBits(AI->getType()) / 8;

    // 使用LLVM的MemoryLocation构造函数
    MemoryLocation Loc = MemoryLocation::get(AI);
    
    // 创建MOP对象
    return std::make_unique<Mop>(MopType::Atomic, Loc, Inst);
  }
  if (AtomicCmpXchgInst* CI = dyn_cast<AtomicCmpXchgInst>(Inst)) {
    // 处理原子比较交换指令
    Value* Ptr = CI->getPointerOperand();
    
    // 获取访问大小
    uint64_t Size = DL.getTypeStoreSizeInBits(CI->getCompareOperand()->getType()) / 8;
    
    // 使用LLVM的MemoryLocation构造函数
    MemoryLocation Loc = MemoryLocation::get(CI);
    
    // 创建MOP对象
    return std::make_unique<Mop>(MopType::Atomic, Loc, Inst);
  }
  if (CallBase* CB = dyn_cast<CallBase>(Inst)) {
    // 处理内存拷贝和设置函数
    Function* CalledFunc = CB->getCalledFunction();
    if (CalledFunc) {
      StringRef FuncName = CalledFunc->getName();
      if (FuncName.startswith("llvm.memcpy") || FuncName.startswith("memcpy")) {
        // 处理内存拷贝
        if (CB->arg_size() >= 3) {
          Value* DstPtr = CB->getArgOperand(0);
          
          // 获取大小参数
          Value* SizeVal = CB->getArgOperand(2);
          LocationSize Size = LocationSize::precise(0);  // 默认使用大小为0
          if (ConstantInt* CI = dyn_cast<ConstantInt>(SizeVal)) {
            Size = LocationSize::precise(CI->getZExtValue());
          }
          
          // 使用LLVM的MemoryLocation构造函数
          MemoryLocation Loc = MemoryLocation(DstPtr, Size);
          
          // 创建MOP对象
          return std::make_unique<Mop>(MopType::Memcpy, Loc, Inst);
        }
      }
      else if (FuncName.startswith("llvm.memset") || FuncName.startswith("memset")) {
        // 处理内存设置
        if (CB->arg_size() >= 3) {
          Value* DstPtr = CB->getArgOperand(0);
          
          // 获取大小参数
          Value* SizeVal = CB->getArgOperand(2);
          LocationSize Size = LocationSize::precise(0);  // 默认使用大小为0
          if (ConstantInt* CI = dyn_cast<ConstantInt>(SizeVal)) {
            Size = LocationSize::precise(CI->getZExtValue());
          }
          
          // 使用LLVM的MemoryLocation构造函数
          MemoryLocation Loc = MemoryLocation(DstPtr, Size);
          
          // 创建MOP对象
          return std::make_unique<Mop>(MopType::Memset, Loc, Inst);
        }
      }
    }
  }
  
  // 不支持的指令类型，返回nullptr
  return nullptr;
}

// 将单个指令转换为MOP
std::unique_ptr<Mop> MopBuilder::buildMop(Instruction* Inst) {
  // 只处理内存访问指令
  if (!Inst->mayReadOrWriteMemory()) {
    return nullptr;
  }
  
  // 使用createMop创建MOP对象
  return createMop(Inst);
}

// 将整个函数转换为MOP列表
MopList MopBuilder::buildMopList() {
  MopList mopList;
  
  // 遍历函数中的所有基本块
  for (BasicBlock& BB : F) {
    // 遍历基本块中的所有指令
    for (Instruction& I : BB) {
      // 尝试将指令转换为MOP
      std::unique_ptr<Mop> mop = buildMop(&I);
      if (mop) {
        // 如果成功转换，添加到列表中
        mopList.push_back(std::move(mop));
      }
    }
  }
  
  return mopList;
}

} // namespace MopIR
} // namespace __xsan