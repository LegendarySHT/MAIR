#ifndef XSAN_MOP_IR_ANNOTATOR_H
#define XSAN_MOP_IR_ANNOTATOR_H

#include "Mop.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include <string>

namespace __xsan {
namespace MopIR {

// 前向声明
class MopIR;

// 注释详细级别
enum class AnnotationLevel {
  Minimal,    // 最小信息：仅 MOP 类型和是否冗余
  Basic,       // 基本信息：类型、访问模式、冗余状态
  Detailed,    // 详细信息：包含所有分析结果
  Full        // 完整信息：包含依赖关系、覆盖关系等所有信息
};

// MopIR 注释器：将 MopIR 信息以 Metadata 形式添加到 LLVM IR 中
class MopIRAnnotator {
private:
  AnnotationLevel Level;           // 注释详细级别
  bool UseMetadata;                // 是否使用 Metadata（否则使用注释字符串）
  llvm::Function* F;               // 目标函数
  
  // Metadata 键名
  static constexpr const char* MopTypeMDName = "xsan.mop.type";
  static constexpr const char* MopPatternMDName = "xsan.mop.pattern";
  static constexpr const char* MopRedundantMDName = "xsan.mop.redundant";
  static constexpr const char* MopCoveringMDName = "xsan.mop.covering";
  static constexpr const char* MopDependenciesMDName = "xsan.mop.dependencies";
  static constexpr const char* MopAccessPatternMDName = "xsan.mop.access_pattern";
  static constexpr const char* MopFullInfoMDName = "xsan.mop.full_info";

public:
  MopIRAnnotator(AnnotationLevel Lvl = AnnotationLevel::Basic, bool UseMD = true)
    : Level(Lvl), UseMetadata(UseMD), F(nullptr) {}
  
  // 为整个 MopIR 添加注释
  void annotate(MopIR& MopIR);
  
  // 为单个 MOP 添加注释
  void annotateMop(Mop* M);
  
  // 设置注释详细级别
  void setAnnotationLevel(AnnotationLevel Lvl) { Level = Lvl; }
  AnnotationLevel getAnnotationLevel() const { return Level; }
  
  // 设置是否使用 Metadata
  void setUseMetadata(bool UseMD) { UseMetadata = UseMD; }
  bool getUseMetadata() const { return UseMetadata; }

private:
  // 将 MOP 类型转换为字符串
  std::string mopTypeToString(MopType Type) const;
  
  // 将访问模式转换为字符串
  std::string accessPatternToString(AccessPattern Pattern) const;
  
  // 生成 MOP 的完整信息字符串
  std::string generateMopInfoString(Mop* M) const;
  
  // 生成 MOP 的简要信息字符串
  std::string generateMopBriefString(Mop* M) const;
  
  // 使用 Metadata 添加注释
  void annotateWithMetadata(llvm::Instruction* Inst, Mop* M);
  
  // 使用注释字符串添加注释（通过 Metadata 中的注释）
  void annotateWithComment(llvm::Instruction* Inst, Mop* M);
  
  // 创建 Metadata 字符串节点
  llvm::MDString* createMDString(const std::string& Str) const;
  
  // 创建 Metadata 节点
  llvm::MDNode* createMDNode(llvm::ArrayRef<llvm::Metadata*> MDs) const;
};

} // namespace MopIR
} // namespace __xsan

#endif // XSAN_MOP_IR_ANNOTATOR_H
