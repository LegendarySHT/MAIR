#include "PassRegistry.h"
#include "AttributeTaggingPass.hpp"

#include "llvm/Passes/PassBuilder.h"

#include "llvm/Transforms/Instrumentation/AddressSanitizer.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizerCommon.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizerOptions.h"

#include "llvm/Transforms/Instrumentation/ThreadSanitizer.h"

using namespace llvm;

/// Set the default value to true
/// From
/// https://github.com/llvm/llvm-project/commit/1ada819c237bf724e6eaa1c82b2742e3eb57a5d5#diff-3fdc9c7b499c6c90240f4409c3ad6bd18cc9b0c247751ab371769101a195238b
static cl::opt<bool> ClAsanUseOdrIndicator(
    "sanitize-address-use-odr-indicator",
    cl::desc("Simulates -fsanitize-address-use-odr-indicator"), cl::Hidden,
    cl::init(true));

static cl::opt<AsanDtorKind> ClAsanDestructorKind(
    "sanitize-address-destructor",
    cl::desc("Simulates -fsanitize-address-destructor"),
    cl::values(clEnumValN(AsanDtorKind::None, "none", "No destructors"),
               clEnumValN(AsanDtorKind::Global, "global",
                          "Use global destructors")),
    cl::init(AsanDtorKind::Global), cl::Hidden);

static cl::opt<bool> ClAsanUseAfterScope(
    "sanitize-address-use-after-scope",
    cl::desc("Simulates -fsanitize-address-use-after-scope"), cl::Hidden,
    cl::init(true));

static cl::opt<AsanDetectStackUseAfterReturnMode> ClAsanUseAfterReturn(
    "sanitize-address-use-after-return",
    cl::desc("Simulates -fsanitize-address-use-after-return"),
    cl::values(
        clEnumValN(AsanDetectStackUseAfterReturnMode::Never, "never",
                   "Never detect stack use after return."),
        clEnumValN(
            AsanDetectStackUseAfterReturnMode::Runtime, "runtime",
            "Detect stack use after return if "
            "binary flag 'ASAN_OPTIONS=detect_stack_use_after_return' is set."),
        clEnumValN(AsanDetectStackUseAfterReturnMode::Always, "always",
                   "Always detect stack use after return.")),
    cl::Hidden, cl::init(AsanDetectStackUseAfterReturnMode::Runtime));

/*
static bool asanUseGlobalsGC(const Triple &T, const CodeGenOptions &CGOpts) {
  if (!CGOpts.SanitizeAddressGlobalsDeadStripping)
    return false;
  switch (T.getObjectFormat()) {
  case Triple::MachO:
  case Triple::COFF:
    return true;
  case Triple::ELF:
    return !CGOpts.DisableIntegratedAS;
  case Triple::GOFF:
    llvm::report_fatal_error("ASan not implemented for GOFF");
  case Triple::XCOFF:
    llvm::report_fatal_error("ASan not implemented for XCOFF.");
  case Triple::Wasm:
  case Triple::DXContainer:
  case Triple::SPIRV:
  case Triple::UnknownObjectFormat:
    break;
  }
  return false;
}
*/
static cl::opt<bool> ClAsanGlobalsGC(
    "asan-globals-gc",
    cl::desc("Controls whether ASan uses gc-friendly globals instrumentation"),
    cl::Hidden, cl::init(false));

static cl::opt<bool>
    ClAsanRecover("sanitize-recover-address",
                  cl::desc("Simulates -fsanitize-recover=address"), cl::Hidden,
                  cl::init(false));


static cl::opt<bool> ClAllRecover("sanitize-recover-all",
                                  cl::desc("Simulates -fsanitize-recover=all"),
                                  cl::Hidden, cl::init(false));

namespace __xsan {

LLVM_ATTRIBUTE_WEAK
bool isAsanTurnedOff() { return false; }
LLVM_ATTRIBUTE_WEAK
bool isTsanTurnedOff() { return false; }

static void addAsanToMPM(ModulePassManager &MPM) {
  if (isAsanTurnedOff())
    return;
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
  Opts.Recover = ClAsanRecover || ClAllRecover;
  // The default value of UseAfterScope is true
  Opts.UseAfterScope = ClAsanUseAfterScope;
  Opts.UseAfterReturn = ClAsanUseAfterReturn;

  llvm::AsanDtorKind DestructorKind = ClAsanDestructorKind;

  bool UseOdrIndicator = ClAsanUseOdrIndicator;
  bool UseGlobalGC = ClAsanGlobalsGC;

  MPM.addPass(ModuleAddressSanitizerPass(Opts, UseGlobalGC, UseOdrIndicator,
                                         DestructorKind));
}

static void addTsanToMPM(ModulePassManager &MPM) {
  if (isTsanTurnedOff())
    return;
  // Create ctor and init functions.
  MPM.addPass(ModuleThreadSanitizerPass());
  // Function Pass to Module Pass. Instruments functions to detect race
  // conditions reads.
  MPM.addPass(createModuleToFunctionPassAdaptor(ThreadSanitizerPass()));
}

void registerAsanForClangAndOpt(PassBuilder &PB) {
  PB.registerOptimizerLastEPCallback(
      [=](ModulePassManager &MPM, OptimizationLevel level) {
        addAsanToMPM(MPM);
      });

  // 这里注册opt回调的名称
  PB.registerPipelineParsingCallback(
      [=](StringRef Name, ModulePassManager &MPM,
          ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "asan") {
          MPM.addPass(AttributeTaggingPass(SanitizerType::ASan));
          addAsanToMPM(MPM);
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
        addTsanToMPM(MPM);
      });

  // 这里注册opt回调的名称
  PB.registerPipelineParsingCallback(
      [=](StringRef Name, ModulePassManager &MPM,
          ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "tsan") {
          MPM.addPass(AttributeTaggingPass(SanitizerType::TSan));
          addTsanToMPM(MPM);
          return true;
        }
        return false;
      });
}

void registerXsanForClangAndOpt(PassBuilder &PB) {
  PB.registerOptimizerLastEPCallback(
      [=](ModulePassManager &MPM, OptimizationLevel level) {
        addAsanToMPM(MPM);
        addTsanToMPM(MPM);
      });

  // 这里注册opt回调的名称
  PB.registerPipelineParsingCallback(
      [=](StringRef Name, ModulePassManager &MPM,
          ArrayRef<PassBuilder::PipelineElement>) {
        if (Name == "xsan") {
          MPM.addPass(AttributeTaggingPass(SanitizerType::ASan));
          MPM.addPass(AttributeTaggingPass(SanitizerType::TSan));
          addAsanToMPM(MPM);
          addTsanToMPM(MPM);
          return true;
        }
        return false;
      });
}

} // namespace __xsan
