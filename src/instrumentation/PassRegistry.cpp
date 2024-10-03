#include "PassRegistry.h"
#include "AttributeTaggingPass.hpp"

#include "llvm/Transforms/Instrumentation/AddressSanitizer.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizerCommon.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizerOptions.h"

#include "llvm/Transforms/Instrumentation/ThreadSanitizer.h"

using namespace llvm;

namespace __xsan {
void registerAsanForClangAndOpt(PassBuilder &PB) {
  /*
   The relevant code in clang BackendUtil.cpp::addSanitizer
    ```cpp
    bool UseGlobalGC = asanUseGlobalsGC(TargetTriple, CodeGenOpts);
    bool UseOdrIndicator = CodeGenOpts.SanitizeAddressUseOdrIndicator;
    llvm::AsanDtorKind DestructorKind =
        CodeGenOpts.getSanitizeAddressDtor();
    AddressSanitizerOptions Opts;
    Opts.CompileKernel = CompileKernel;
    Opts.Recover = CodeGenOpts.SanitizeRecover.has(Mask);
    Opts.UseAfterScope = CodeGenOpts.SanitizeAddressUseAfterScope;
    Opts.UseAfterReturn = CodeGenOpts.getSanitizeAddressUseAfterReturn();
    ```
  */
  AddressSanitizerOptions Opts;
  // Here set the default value for each setting, but you can set them by
  // their cl::opt version.
  Opts.CompileKernel = false;
  Opts.Recover = false;
  // The default value of UseAfterScope is true
  Opts.UseAfterScope = true;
  Opts.UseAfterReturn = AsanDetectStackUseAfterReturnMode::Runtime;

  llvm::AsanDtorKind DestructorKind = llvm::AsanDtorKind::Global;
  bool UseOdrIndicator = false;
  bool UseGlobalGC = false;

  // // 这里注册 clang plugin extension point
  // // FIXME: what if LTO? this EP is not suitable for LTO.
  // PB.registerPipelineStartEPCallback(
  //   [](ModulePassManager &MPM, auto _) {
  //     MPM.addPass(AttributeTaggingPass(SanitizerType::ASan));
  //   }
  // );

  PB.registerOptimizerLastEPCallback([=](ModulePassManager &MPM,
                                         OptimizationLevel level) {
    MPM.addPass(ModuleAddressSanitizerPass(Opts, UseGlobalGC, UseOdrIndicator,
                                           DestructorKind));
  });

  // 这里注册opt回调的名称
  PB.registerPipelineParsingCallback(
      [=](StringRef Name, ModulePassManager &MPM,
          ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "asan") {
          MPM.addPass(AttributeTaggingPass(SanitizerType::ASan));
          MPM.addPass(ModuleAddressSanitizerPass(
              Opts, UseGlobalGC, UseOdrIndicator, DestructorKind));
          return true;
        }
        return false;
      });
}

void registerTsanForClangAndOpt(PassBuilder &PB) {
  // // 这里注册 clang plugin extension point
  // // FIXME: what if LTO? this EP is not suitable for LTO.
  // PB.registerPipelineStartEPCallback(
  //   [](ModulePassManager &MPM, auto _) {
  //     MPM.addPass(AttributeTaggingPass(SanitizerType::TSan));
  //   }
  // );

  PB.registerOptimizerLastEPCallback(
      [=](ModulePassManager &MPM, OptimizationLevel level) {
        // Create ctor and init functions.
        MPM.addPass(ModuleThreadSanitizerPass());
        // Function Pass to Module Pass. Instruments functions to detect race
        // conditions reads.
        MPM.addPass(createModuleToFunctionPassAdaptor(ThreadSanitizerPass()));
      });

  // 这里注册opt回调的名称
  PB.registerPipelineParsingCallback(
      [=](StringRef Name, ModulePassManager &MPM,
          ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "tsan") {
          MPM.addPass(AttributeTaggingPass(SanitizerType::TSan));
          // Create ctor and init functions.
          MPM.addPass(ModuleThreadSanitizerPass());
          // Function Pass to Module Pass. Instruments functions to detect race
          // conditions reads.
          MPM.addPass(createModuleToFunctionPassAdaptor(ThreadSanitizerPass()));
          return true;
        }
        return false;
      });
}
} // namespace __xsan
