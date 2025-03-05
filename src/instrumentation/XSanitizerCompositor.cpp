#include "AttributeTaggingPass.hpp"
#include "Instrumentation.h"
#include "PassRegistry.h"
#include "Utils/Logging.h"
#include "Utils/Options.h"
#include "xsan_common.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

using namespace llvm;
using namespace __xsan;

// static cl::opt<bool> ClLog(
//     "xsan-post-opt", cl::init(true),
//     cl::desc("Whether to perform post-sanitziers optimizations for XSan"),
//     cl::Hidden);

namespace __xsan {

class SanitizerCompositorPass
    : public llvm::PassInfoMixin<SanitizerCompositorPass> {
public:
  SanitizerCompositorPass(OptimizationLevel level);
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &AM);
  //   void printPipeline(llvm::raw_ostream &OS,
  //                      llvm::function_ref<llvm::StringRef(llvm::StringRef)>
  //                      MapClassName2PassName);
  static bool isRequired() { return true; }

private:
  OptimizationLevel Level;
};

void registerXsanForClangAndOpt(llvm::PassBuilder &PB) {
  registerAnalysisForXsan(PB);

  PB.registerOptimizerLastEPCallback(
      [=](ModulePassManager &MPM, OptimizationLevel level) {
        MPM.addPass(SanitizerCompositorPass(level));
      });

  // 这里注册opt回调的名称
  PB.registerPipelineParsingCallback(
      [=](StringRef Name, ModulePassManager &MPM,
          ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "xsan") {
          MPM.addPass(AttributeTaggingPass(SanitizerType::ASan));
          MPM.addPass(AttributeTaggingPass(SanitizerType::TSan));
          MPM.addPass(SanitizerCompositorPass(OptimizationLevel::O0));
          return true;
        }
        return false;
      });
}

} // namespace __xsan

SanitizerCompositorPass::SanitizerCompositorPass(OptimizationLevel level)
    : Level(level) {}

PreservedAnalyses SanitizerCompositorPass::run(Module &M,
                                               ModuleAnalysisManager &MAM) {
  options::ClDebug.setValue(options::ClDebug || !!std::getenv("XSAN_DEBUG"));

  FunctionAnalysisManager &FAM =
      MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();

  LoopOptLeval level = options::opt::loopOptLevel();
  if (level != LoopOptLeval::NoOpt) {
    for (auto &F : M) {
      if (F.isDeclaration() || F.empty())
        continue;
      LoopMopInstrumenter LoopInstrumenter(F, FAM, level);
      LoopInstrumenter.instrument();
    }
  }

  SubSanitizers Sanitizers = SubSanitizers::loadSubSanitizers();
  /// Unlike ModulePassManager, SubSanitizers does not invalidate Analysises
  /// between the runnings of sanitizers' passes.
  PreservedAnalyses PA = Sanitizers.run(M, MAM);
  if (Level.getSpeedupLevel() > 0 && options::opt::enablePostOpt()) {
    ModulePassManager PostOpts = createPostOptimizationPasses(Level);
    /// Invalidate Analyses to re-run them with the post-optimization passes.
    MAM.invalidate(M, PA);
    for (auto &F : M) {
      FAM.invalidate(F, PA);
    }
    PostOpts.run(M, MAM);
  }

  Log.displayLogs(M.getName());

  return PA;
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "XSan Composition Pass", LLVM_VERSION_STRING,
          __xsan::registerXsanForClangAndOpt};
}