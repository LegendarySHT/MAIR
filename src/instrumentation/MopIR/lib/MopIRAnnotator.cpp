#include "../include/MopIRAnnotator.h"
#include "../include/MopIR.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/raw_ostream.h"
#include <sstream>

using namespace __xsan::MopIR;
using namespace llvm;

// 将 MOP 类型转换为字符串
std::string MopIRAnnotator::mopTypeToString(MopType Type) const {
  switch (Type) {
    case MopType::Load:   return "Load";
    case MopType::Store:  return "Store";
    case MopType::Atomic: return "Atomic";
    case MopType::Memcpy: return "Memcpy";
    case MopType::Memset: return "Memset";
    default:              return "Unknown";
  }
}

// 将访问模式转换为字符串
std::string MopIRAnnotator::accessPatternToString(AccessPattern Pattern) const {
  switch (Pattern) {
    case AccessPattern::Unknown:  return "Unknown";
    case AccessPattern::Simple:  return "Simple";
    case AccessPattern::Range:   return "Range";
    case AccessPattern::Periodic: return "Periodic";
    case AccessPattern::Cyclic:   return "Cyclic";
    default:                      return "Unknown";
  }
}

// 生成 MOP 的简要信息字符串
std::string MopIRAnnotator::generateMopBriefString(Mop* M) const {
  std::stringstream SS;
  SS << "MOP[" << mopTypeToString(M->getType());
  
  if (M->isRedundant()) {
    SS << ", REDUNDANT";
  }
  
  if (M->hasKnownAccessPattern()) {
    SS << ", Pattern:" << accessPatternToString(M->getAccessPattern().Pattern);
  }
  
  SS << "]";
  return SS.str();
}

// 生成 MOP 的完整信息字符串
std::string MopIRAnnotator::generateMopInfoString(Mop* M) const {
  std::stringstream SS;
  
  SS << "=== MOP Information ===\n";
  SS << "Type: " << mopTypeToString(M->getType()) << "\n";
  SS << "Read/Write: " << (M->isRead() ? "Read" : (M->isWrite() ? "Write" : "Unknown")) << "\n";
  
  // 内存位置信息
  const auto& Loc = M->getLocation();
  SS << "Memory Location:\n";
  SS << "  Ptr: " << (Loc.Ptr ? Loc.Ptr->getName().str() : "<null>") << "\n";
  if (Loc.Size.hasValue()) {
    SS << "  Size: " << Loc.Size.getValue() << " bytes\n";
  } else {
    SS << "  Size: unknown\n";
  }
  
  // 冗余信息
  SS << "Redundant: " << (M->isRedundant() ? "Yes" : "No") << "\n";
  if (M->isRedundant() && M->getCoveringMop()) {
    SS << "Covered by: MOP@" << M->getCoveringMop() << "\n";
  }
  
  // 访问模式信息
  const auto& Pattern = M->getAccessPattern();
  SS << "Access Pattern: " << accessPatternToString(Pattern.Pattern) << "\n";
  
  if (Pattern.isRangeAccess()) {
    SS << "  Range: [" << (Pattern.RangeBegin ? "Begin" : "null") 
       << ", " << (Pattern.RangeEnd ? "End" : "null") << ")\n";
  } else if (Pattern.isPeriodicAccess()) {
    SS << "  Periodic: [" << (Pattern.PeriodicBegin ? "Begin" : "null")
       << ", " << (Pattern.PeriodicEnd ? "End" : "null") << ")\n";
    SS << "  AccessSize: " << Pattern.AccessSize << " bytes\n";
    SS << "  Step: " << (Pattern.StepIsConstant ? 
                          std::to_string(Pattern.StepConstant) : "variable") << "\n";
  } else if (Pattern.isCyclicAccess()) {
    SS << "  Cyclic: [" << (Pattern.CyclicBegin ? "Begin" : "null")
       << ", " << (Pattern.CyclicEnd ? "End" : "null") << ")\n";
    SS << "  Access: [" << (Pattern.CyclicAccessBeg ? "Beg" : "null")
       << ", " << (Pattern.CyclicAccessEnd ? "End" : "null") << ")\n";
  }
  
  // 依赖关系
  const auto& Deps = M->getDependencies();
  if (!Deps.empty()) {
    SS << "Dependencies: " << Deps.size() << " MOP(s)\n";
  }
  
  SS << "======================";
  return SS.str();
}

// 创建 Metadata 字符串节点
MDString* MopIRAnnotator::createMDString(const std::string& Str) const {
  if (!F) return nullptr;
  return MDString::get(F->getContext(), Str);
}

// 创建 Metadata 节点
MDNode* MopIRAnnotator::createMDNode(ArrayRef<Metadata*> MDs) const {
  if (!F) return nullptr;
  return MDNode::get(F->getContext(), MDs);
}

// 为整个 MopIR 添加注释
void MopIRAnnotator::annotate(MopIR& MopIR) {
  F = &MopIR.getContext().getFunction();
  
  for (auto& Mop : MopIR.getOptimizedMops()) {
    if (Mop && Mop->getOriginalInst()) {
      annotateMop(Mop.get());
    }
  }
}

// 为单个 MOP 添加注释
void MopIRAnnotator::annotateMop(Mop* M) {
  if (!M || !M->getOriginalInst() || !F) {
    return;
  }
  
  Instruction* Inst = M->getOriginalInst();
  
  if (UseMetadata) {
    annotateWithMetadata(Inst, M);
  } else {
    annotateWithComment(Inst, M);
  }
}

// 使用 Metadata 添加注释
void MopIRAnnotator::annotateWithMetadata(Instruction* Inst, Mop* M) {
  LLVMContext& Ctx = F->getContext();
  
  // 根据详细级别添加不同的 Metadata
  switch (Level) {
    case AnnotationLevel::Minimal: {
      // 仅添加类型和冗余状态
      auto* TypeMD = createMDString(mopTypeToString(M->getType()));
      auto* RedundantMD = createMDString(M->isRedundant() ? "true" : "false");
      if (TypeMD && RedundantMD) {
        auto* MopMD = createMDNode({TypeMD, RedundantMD});
        Inst->setMetadata(MopTypeMDName, MopMD);
      }
      break;
    }
    
    case AnnotationLevel::Basic: {
      // 添加类型、访问模式和冗余状态
      auto* TypeMD = createMDString(mopTypeToString(M->getType()));
      auto* PatternMD = createMDString(accessPatternToString(M->getAccessPattern().Pattern));
      auto* RedundantMD = createMDString(M->isRedundant() ? "true" : "false");
      
      if (TypeMD && PatternMD && RedundantMD) {
        auto* MopMD = createMDNode({TypeMD, PatternMD, RedundantMD});
        Inst->setMetadata(MopTypeMDName, MopMD);
      }
      break;
    }
    
    case AnnotationLevel::Detailed: {
      // 添加详细信息
      auto* BriefMD = createMDString(generateMopBriefString(M));
      if (BriefMD) {
        Inst->setMetadata(MopFullInfoMDName, createMDNode({BriefMD}));
      }
      break;
    }
    
    case AnnotationLevel::Full: {
      // 添加完整信息
      auto* FullInfoMD = createMDString(generateMopInfoString(M));
      if (FullInfoMD) {
        Inst->setMetadata(MopFullInfoMDName, createMDNode({FullInfoMD}));
      }
      
      // 分别添加各个字段的 Metadata
      auto* TypeMD = createMDString(mopTypeToString(M->getType()));
      auto* PatternMD = createMDString(accessPatternToString(M->getAccessPattern().Pattern));
      auto* RedundantMD = createMDString(M->isRedundant() ? "true" : "false");
      
      if (TypeMD) {
        Inst->setMetadata(MopTypeMDName, createMDNode({TypeMD}));
      }
      if (PatternMD) {
        Inst->setMetadata(MopPatternMDName, createMDNode({PatternMD}));
      }
      if (RedundantMD) {
        Inst->setMetadata(MopRedundantMDName, createMDNode({RedundantMD}));
      }
      
      // 添加依赖关系
      const auto& Deps = M->getDependencies();
      if (!Deps.empty()) {
        SmallVector<Metadata*, 8> DepMDs;
        for (auto* Dep : Deps) {
          if (Dep && Dep->getOriginalInst()) {
            // 使用指令的地址作为标识符
            std::stringstream SS;
            SS << "MOP@" << static_cast<const void*>(Dep->getOriginalInst());
            auto* DepMD = createMDString(SS.str());
            if (DepMD) {
              DepMDs.push_back(DepMD);
            }
          }
        }
        if (!DepMDs.empty()) {
          Inst->setMetadata(MopDependenciesMDName, createMDNode(DepMDs));
        }
      }
      
      // 添加覆盖关系
      if (M->isRedundant() && M->getCoveringMop() && 
          M->getCoveringMop()->getOriginalInst()) {
        std::stringstream SS;
        SS << "MOP@" << static_cast<const void*>(M->getCoveringMop()->getOriginalInst());
        auto* CoveringMD = createMDString(SS.str());
        if (CoveringMD) {
          Inst->setMetadata(MopCoveringMDName, createMDNode({CoveringMD}));
        }
      }
      break;
    }
  }
}

// 使用注释字符串添加注释（通过 Metadata）
void MopIRAnnotator::annotateWithComment(Instruction* Inst, Mop* M) {
  // 使用 Metadata 存储注释字符串（这样可以在 IR 转储时看到）
  // 注意：LLVM IR 本身不支持注释，我们通过 Metadata 来模拟
  std::string Comment;
  
  switch (Level) {
    case AnnotationLevel::Minimal:
      Comment = generateMopBriefString(M);
      break;
    case AnnotationLevel::Basic:
      Comment = generateMopBriefString(M);
      break;
    case AnnotationLevel::Detailed:
    case AnnotationLevel::Full:
      Comment = generateMopInfoString(M);
      // 将多行注释中的换行符替换为特殊标记，以便在 IR 转储时识别
      break;
  }
  
  // 将注释作为 Metadata 添加
  auto* CommentMD = createMDString(Comment);
  if (CommentMD) {
    Inst->setMetadata("xsan.mop.comment", createMDNode({CommentMD}));
  }
}
