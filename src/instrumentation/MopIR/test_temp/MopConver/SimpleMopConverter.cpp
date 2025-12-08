// 该文件仅用作对MopIR转换器的简单测试


#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "../../include/MopBuilder.h"
#include "../../include/Mop.h"

using namespace llvm;
using namespace __xsan::MopIR;

// 简化版本，不使用PassBuilder
int main(int argc, char** argv) {
  // 初始化LLVM上下文
  LLVMContext Context;
  SMDiagnostic Error;
  
  // 读取LLVM IR文件 - 使用simple_test.ll
  std::unique_ptr<Module> M = parseIRFile("simple_test.ll", Error, Context);
  if (!M) {
    Error.print("SimpleMopConverter", errs());
    return 1;
  }
  
  // 遍历模块中的所有函数并转换为Mop IR
  for (Function& F : *M) {
    if (F.isDeclaration()) continue; // 跳过函数声明
    
    outs() << "\n=== Processing function: " << F.getName() << " ===\n";
    
    // 创建MopBuilder并构建MopList
    MopBuilder Builder(F);
    MopList Mops = Builder.buildMopList();
    
    // 输出转换结果
    outs() << "Total Mops: " << Mops.size() << "\n";
    
    for (size_t i = 0; i < Mops.size(); ++i) {
      outs() << "\nMop " << i << ":\n";
      Mops[i]->print(outs());
      outs() << "\n";
    }
    
    outs() << "\nFunction conversion completed!\n";
    outs() << "-------------------------------------------\n";
  }
  
  outs() << "\nAll functions converted successfully!\n";
  return 0;
}