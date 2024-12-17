#include "PassRegistry.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool> ClDisableAsan(
    "xsan-disable-asan", cl::init(false),
    cl::desc("Do not use ASan's instrumentation"),
    cl::Hidden);

static cl::opt<bool> ClDisableTsan(
    "xsan-disable-tsan", cl::init(false),
    cl::desc("Do not use TSan's instrumentation"),
    cl::Hidden);

namespace __xsan {

bool isAsanTurnedOff() { return ClDisableAsan; }
bool isTsanTurnedOff() { return ClDisableTsan; }

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