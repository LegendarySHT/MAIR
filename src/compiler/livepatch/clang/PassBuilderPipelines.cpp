/*
Hot patch to modify the sanitizer passes pipeline
*/

#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/StringSet.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Target/TargetMachine.h>

#include <utility>

#include "utils/PatchHelper.h"
using namespace llvm;

namespace {

const StringSet<> HackedModulePasses = {"ModuleAddressSanitizerPass",
                                        "ModuleMemorySanitizerPass",
                                        "ModuleThreadSanitizerPass"};
const StringSet<> HackedFunctionPasses = {"ThreadSanitizerPass",
                                          "MemorySanitizerPass"};

class MPMRewriter : public ModulePassManager {
public:
  MPMRewriter(ModulePassManager &&MPM)
      : ModulePassManager(std::forward<ModulePassManager>(MPM)) {

    /// TODO: Remove and add passes here
    // for (auto &P : Passes) {
    //   if (P->name() == "ModuleToFunctionPassAdaptor") {
    //     auto *p = (ModuleToFunctionPassAdaptor *)&P;
    //     p->printPipeline(outs(), [](StringRef PassName) { return PassName;
    //     }); outs() << "\n";
    //   }
    //   OKF("Pass: %s", P->name().str().c_str());
    // }
  }

private:
};

ModulePassManager modifySanitizerPassesPipeline(ModulePassManager &MPM) {
  if (!isXsanEnabled() || !getXsanMask()[XSan])
    return std::move(MPM);
  ModulePassManager NewMPM;
  ::MPMRewriter Rewriter(std::move(MPM));
  return Rewriter;
}
} // namespace

static XsanInterceptor buildPerModuleDefaultPipelineInterceptor(
    &PassBuilder::buildPerModuleDefaultPipeline);
static XsanInterceptor buildLTOPreLinkDefaultPipelineInterceptor(
    &PassBuilder::buildLTOPreLinkDefaultPipeline);
static XsanInterceptor buildThinLTOPreLinkDefaultPipelineInterceptor(
    &PassBuilder::buildThinLTOPreLinkDefaultPipeline);
static XsanInterceptor
    buildO0DefaultPipelineInterceptor(&PassBuilder::buildO0DefaultPipeline);

ModulePassManager
PassBuilder::buildPerModuleDefaultPipeline(OptimizationLevel Level,
                                           bool LTOPreLink) {
  ModulePassManager MPM =
      buildPerModuleDefaultPipelineInterceptor(this, Level, LTOPreLink);
  return ::modifySanitizerPassesPipeline(MPM);
}

ModulePassManager
PassBuilder::buildLTOPreLinkDefaultPipeline(OptimizationLevel Level) {
  ModulePassManager MPM =
      buildLTOPreLinkDefaultPipelineInterceptor(this, Level);
  return ::modifySanitizerPassesPipeline(MPM);
}

ModulePassManager
PassBuilder::buildThinLTOPreLinkDefaultPipeline(OptimizationLevel Level) {
  ModulePassManager MPM =
      buildThinLTOPreLinkDefaultPipelineInterceptor(this, Level);
  return ::modifySanitizerPassesPipeline(MPM);
}

ModulePassManager PassBuilder::buildO0DefaultPipeline(OptimizationLevel Level,
                                                      bool LTOPreLink) {
  ModulePassManager MPM =
      buildO0DefaultPipelineInterceptor(this, Level, LTOPreLink);
  return ::modifySanitizerPassesPipeline(MPM);
}
