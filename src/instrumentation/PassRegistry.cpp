#include "PassRegistry.h"
#include "AttributeTaggingPass.hpp"

#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizer.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizerOptions.h"
#include "llvm/Transforms/Instrumentation/ThreadSanitizer.h"
#include "llvm/Transforms/Scalar/EarlyCSE.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/InstSimplifyPass.h"
#include "llvm/Transforms/Scalar/LoopLoadElimination.h"
#include "llvm/Transforms/Scalar/LoopSink.h"
#include "llvm/Transforms/Scalar/SCCP.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"
#include "llvm/Transforms/Scalar/TailRecursionElimination.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"

using namespace llvm;

// -- These middle-end options are used to simulate the frontend options --

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

/// In LLVM 15, this option is false by default.
//    As a workaround for a bug in gold 2.26 and earlier, dead stripping of
//    globals in ASan is disabled by default on most ELF targets.
//    See https://sourceware.org/bugzilla/show_bug.cgi?id=19002
/// In LLVM 20, this option is true by default.
//  Commit:
//    https://github.com/llvm/llvm-project/commit/a8d3ae712290d6f85db2deb9164181058f5c1307#diff-6f3485aafcf75a6f836f79dc7ce698a27780a0942b69d68c105154d905e01cb0R244
static cl::opt<bool> ClAsanGlobalsGC(
    "asan-globals-gc",
    cl::desc("Controls whether ASan uses gc-friendly globals instrumentation"),
    cl::Hidden, cl::init(true));

static cl::opt<bool>
    ClAsanRecover("sanitize-recover-address",
                  cl::desc("Simulates -fsanitize-recover=address"), cl::Hidden,
                  cl::init(false));

static cl::opt<bool> ClAllRecover("sanitize-recover-all",
                                  cl::desc("Simulates -fsanitize-recover=all"),
                                  cl::Hidden, cl::init(false));

// -- These middle-end options are used to perform optimization --
static cl::opt<bool> ClOptXsan("xsan-opt", cl::desc("Optimize instrumentation"),
                               cl::Hidden, cl::init(true));

static cl::opt<bool>
    ClOptTsanLoadStore("xsan-tsan-opt-load-store",
                       cl::desc("Reduce recurring load/stores"), cl::Hidden,
                       cl::init(true));

static cl::opt<bool>
    ClOptAsanLoadStore("xsan-asan-opt-load-store",
                       cl::desc("Reduce recurring load/stores"), cl::Hidden,
                       cl::init(true));

/// There is compile bug for testcase init-order-dlopen.cpp, disable this option
/// for now.
static cl::opt<bool>
    ClAsanPoisonInternalGlobal("xsan-asan-poison-internal-global",
                               cl::desc("Poison internal globals with ASan"),
                               cl::Hidden, cl::init(false));
namespace __xsan {

bool shouldTsanOptimizeLoadStores() { return ClOptXsan && ClOptTsanLoadStore; }

bool shouldAsanOptimizeLoadStores() { return ClOptXsan && ClOptAsanLoadStore; }

bool shouldAsanPoisonInternalGlobals() { return ClAsanPoisonInternalGlobal; }

LLVM_ATTRIBUTE_WEAK
bool isAsanTurnedOff() { return false; }
LLVM_ATTRIBUTE_WEAK
bool isTsanTurnedOff() { return false; }

void obtainAsanPassArgs(AddressSanitizerOptions &Opts, bool &UseGlobalGC,
                        bool &UseOdrIndicator,
                        llvm::AsanDtorKind &DestructorKind) {
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
  // Here set the default value for each setting, but you can set them by
  // their cl::opt version.
  Opts.CompileKernel = false;
  Opts.Recover = ClAsanRecover || ClAllRecover;
  // The default value of UseAfterScope is true
  Opts.UseAfterScope = ClAsanUseAfterScope;
  Opts.UseAfterReturn = ClAsanUseAfterReturn;

  DestructorKind = ClAsanDestructorKind;

  UseOdrIndicator = ClAsanUseOdrIndicator;
  UseGlobalGC = ClAsanGlobalsGC;
}

template <typename PassTy> static PassTy getASanPass() {
  llvm::AddressSanitizerOptions Opts;

  llvm::AsanDtorKind DestructorKind;

  bool UseOdrIndicator;
  bool UseGlobalGC;

  obtainAsanPassArgs(Opts, UseGlobalGC, UseOdrIndicator, DestructorKind);

  return PassTy(Opts, UseGlobalGC, UseOdrIndicator, DestructorKind);
}

PreservedAnalyses SubSanitizers::run(Module &IR, ModuleAnalysisManager &AM) {
  PreservedAnalyses PA = PreservedAnalyses::all();

  for (unsigned Idx = 0, Size = Passes.size(); Idx != Size; ++Idx) {
    auto *P = Passes[Idx].get();

    PreservedAnalyses PassPA;
    {
      TimeTraceScope TimeScope(P->name(), IR.getName());
      PassPA = P->run(IR, AM);
    }

    // // Update the analysis manager as each pass runs and potentially
    // // invalidates analyses.
    // AM.invalidate(IR, PassPA);

    // Finally, intersect the preserved analyses to compute the aggregate
    // preserved set for this pass manager.
    PA.intersect(std::move(PassPA));
  }

  // Invalidation was handled after each pass in the above loop for the
  // current unit of IR. Therefore, the remaining analysis results in the
  // AnalysisManager are preserved. We mark this with a set so that we don't
  // need to inspect each one individually.
  PA.preserveSet<AllAnalysesOn<Module>>();

  return PA;
}

SubSanitizers SubSanitizers::loadSubSanitizers() {
  SubSanitizers Sanitizers;
  // ---------- Collect targets to instrument first ----------------
  FunctionPassManager FPM;
  if (!isAsanTurnedOff()) {
    addAsanRequireAnalysisPass(Sanitizers, FPM);
  }
  if (!isTsanTurnedOff()) {
    addTsanRequireAnalysisPass(Sanitizers, FPM);
  }
  Sanitizers.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));

  // ------------ Then instrument ----------------------------------
  if (!isAsanTurnedOff()) {
    Sanitizers.addPass(getASanPass<ModuleAddressSanitizerPass>());
  }

  if (!isTsanTurnedOff()) {
    Sanitizers.addPass(ModuleThreadSanitizerPass());
    Sanitizers.addPass(
        createModuleToFunctionPassAdaptor(ThreadSanitizerPass()));
  }

  return Sanitizers;
}

/// TODO: explore a more efficient optimization pipeline for XSAN.
llvm::ModulePassManager createPostOptimizationPasses(OptimizationLevel Level) {
  ModulePassManager MPM;
  FunctionPassManager FPM;

  // PromotePass: promote alloca/load/store instructions to registers, 
  // i.e., -mem2reg
  FPM.addPass(PromotePass());
  // SimplifyCFG: simplify the CFG to improve optimization
  FPM.addPass(SimplifyCFGPass());
  // InstSimplify: simplify instructions
  FPM.addPass(InstSimplifyPass());
  // EarlyCSE: eliminate common subexpressions that are not loop-invariant
  FPM.addPass(EarlyCSEPass());
  FPM.addPass(SimplifyCFGPass());
  // InstCombine: peep-hole optiimization of instructions
  FPM.addPass(InstCombinePass());
  FPM.addPass(SimplifyCFGPass());

  if (Level.getSpeedupLevel() >= 2) {
    // GVN: global value numbering, is a O2 optimization
    // eliminates inter-blocks redundant computations by numbering.
    FPM.addPass(GVNPass());
    FPM.addPass(InstCombinePass());
    FPM.addPass(SimplifyCFGPass());
  }

  /* Loop Optimizations */

  // LoopSimplify: simplify loops
  FPM.addPass(LoopSimplifyPass());
  // LCSSA: convert loops to static single assignment form
  FPM.addPass(LCSSAPass());
  // SCCPPass: detect and optimize single-condition critical sections
  FPM.addPass(SCCPPass());
  // LoopLoadElimination: load elimination in loops
  FPM.addPass(LoopLoadEliminationPass());
  // LoopSink: sinking some inst in loops
  FPM.addPass(LoopSinkPass());

  // // LICM: loop invariant code motion (O3 optimization)
  // LICMPass pas;
  // FPM.addPass(LICMPass());

  /* Inst Combine */
  // InstCombine: combine redundant instructions
  FPM.addPass(InstCombinePass());
  // SimplifyCFG: simplify the CFG after instcombine
  FPM.addPass(SimplifyCFGPass());

  /* Tail Call Elimination */
  /// TODO: figrue out is it OKay?
  FPM.addPass(TailCallElimPass());

  MPM.addPass(createModuleToFunctionPassAdaptor(std::move(FPM)));
  return MPM;
}

static void addAsanToMPM(ModulePassManager &MPM) {
  if (isAsanTurnedOff())
    return;

  MPM.addPass(getASanPass<ModuleAddressSanitizerPass>());
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
  registerAnalysisForAsan(PB);

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
  registerAnalysisForTsan(PB);
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

void registerAnalysisForXsan(PassBuilder &PB) {
  if (!isAsanTurnedOff()) {
    registerAnalysisForAsan(PB);
  }
  if (!isTsanTurnedOff()) {
    registerAnalysisForTsan(PB);
  }
}

} // namespace __xsan
