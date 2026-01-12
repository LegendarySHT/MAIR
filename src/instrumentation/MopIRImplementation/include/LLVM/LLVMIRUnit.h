#ifndef MOP_IR_IMPLEMENTATION_LLVM_IR_UNIT_H
#define MOP_IR_IMPLEMENTATION_LLVM_IR_UNIT_H

#ifdef MOP_IR_USE_LLVM

#include "IRUnit.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include <string>

namespace MopIRImpl {
namespace LLVM {

/**
 * LLVM Function IRUnit 包装器
 * 
 * 将 LLVM Function 包装为 IRUnit，使其可以在 Pass Manager 中使用
 */
class FunctionIRUnit : public MopIRImpl::IRUnit {
private:
  llvm::Function& F;
  
public:
  explicit FunctionIRUnit(llvm::Function& Func) : F(Func) {}
  
  std::string getName() const override {
    return F.getName().str();
  }
  
  bool isValid() const override {
    return !F.isDeclaration() && !F.empty();
  }
  
  void print() const override {
    F.print(llvm::errs());
  }
  
  // LLVM 特定访问器
  llvm::Function& getFunction() { return F; }
  const llvm::Function& getFunction() const { return F; }
};

/**
 * LLVM Module IRUnit 包装器
 * 
 * 将 LLVM Module 包装为 IRUnit
 */
class ModuleIRUnit : public MopIRImpl::IRUnit {
private:
  llvm::Module& M;
  
public:
  explicit ModuleIRUnit(llvm::Module& Mod) : M(Mod) {}
  
  std::string getName() const override {
    return M.getName().str();
  }
  
  bool isValid() const override {
    return true; // Module 总是有效的
  }
  
  void print() const override {
    M.print(llvm::errs(), nullptr);
  }
  
  // LLVM 特定访问器
  llvm::Module& getModule() { return M; }
  const llvm::Module& getModule() const { return M; }
};

} // namespace LLVM
} // namespace MopIRImpl

#endif // MOP_IR_USE_LLVM

#endif // MOP_IR_IMPLEMENTATION_LLVM_IR_UNIT_H

