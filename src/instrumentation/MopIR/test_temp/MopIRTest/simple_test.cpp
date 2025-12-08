#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

int main() {
  outs() << "Testing basic LLVM functionality...\n";

  LLVMContext Context;
  outs() << "LLVM context created\n";

  Module M("test", Context);
  outs() << "Module created\n";

  outs() << "Test completed successfully!\n";
  return 0;
}