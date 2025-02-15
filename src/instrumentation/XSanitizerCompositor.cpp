#include "AttributeTaggingPass.hpp"
#include "Instrumentation.h"
#include "PassRegistry.h"
#include "xsan_common.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;
using namespace __xsan;

static cl::opt<bool> ClDisableAsan(
    "xsan-disable-asan", cl::init(false),
    cl::desc("Do not use ASan's instrumentation"),
    cl::Hidden);

static cl::opt<bool> ClDisableTsan(
    "xsan-disable-tsan", cl::init(false),
    cl::desc("Do not use TSan's instrumentation"),
    cl::Hidden);

static cl::opt<LoopOptLeval> ClLoopOpt(
    "xsan-loop-opt", cl::desc("Loop optimization level for XSan"),
    cl::values(
        clEnumValN(LoopOptLeval::NoOpt, "no",
                   "Disable loop optimization for XSan"),
        clEnumValN(LoopOptLeval::CombineToRangeCheck, "range",
                   "Only combine periodic checks to range check for XSan"),
        clEnumValN(LoopOptLeval::CombinePeriodicChecks, "periodic",
                   "Only combine periodic checks for XSan"),
        clEnumValN(LoopOptLeval::Full, "full",
                   "Enable all loop optimization for XSan")),
    cl::Hidden, cl::init(LoopOptLeval::Full));

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

void registerXsanForClangAndOpt(llvm::PassBuilder &PB) {
  registerAnalysisForXsan(PB);

  PB.registerOptimizerLastEPCallback(
      [=](ModulePassManager &MPM, OptimizationLevel level) {
        MPM.addPass(SanitizerCompositorPass());
      });

  // 这里注册opt回调的名称
  PB.registerPipelineParsingCallback(
      [=](StringRef Name, ModulePassManager &MPM,
          ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "xsan") {
          MPM.addPass(AttributeTaggingPass(SanitizerType::ASan));
          MPM.addPass(AttributeTaggingPass(SanitizerType::TSan));
          MPM.addPass(SanitizerCompositorPass());
          return true;
        }
        return false;
      });
}

} // namespace __xsan

SanitizerCompositorPass::SanitizerCompositorPass() {}

PreservedAnalyses SanitizerCompositorPass::run(Module &M,
                                               ModuleAnalysisManager &MAM) {
  FunctionAnalysisManager &FAM =
      MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  for (auto &F : M) {
    LoopMopInstrumenter LoopInstrumenter(F, FAM, ClLoopOpt);
    LoopInstrumenter.instrument();
  }

  SubSanitizers Sanitizers = __xsan::loadSubSanitizers();
  /// Unlike ModulePassManager, SubSanitizers does not invalidate Analysises
  /// between the runnings of sanitizers' passes.
  PreservedAnalyses PA = Sanitizers.run(M, MAM);
  return PA;
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "XSan Composition Pass", LLVM_VERSION_STRING,
          __xsan::registerXsanForClangAndOpt};
}