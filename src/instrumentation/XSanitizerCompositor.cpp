#include "PassRegistry.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizer.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizerCommon.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizerOptions.h"

namespace __xsan {
class SanitizerCompositorPass
    : public llvm::PassInfoMixin<SanitizerCompositorPass> {
public:
  SanitizerCompositorPass();
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
  //   void printPipeline(llvm::raw_ostream &OS,
  //                      llvm::function_ref<llvm::StringRef(llvm::StringRef)>
  //                      MapClassName2PassName);
  static bool isRequired() { return true; }
};
} // namespace __xsan

using namespace llvm;
using namespace __xsan;

SanitizerCompositorPass::SanitizerCompositorPass() {}

PreservedAnalyses SanitizerCompositorPass::run(Module &M,
                                               ModuleAnalysisManager &AM) {
  /// Now this pass do nothing, just pending for the future.
  return PreservedAnalyses::all();
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "XSan Composition Pass", LLVM_VERSION_STRING,
          __xsan::registerXsanForClangAndOpt};
}