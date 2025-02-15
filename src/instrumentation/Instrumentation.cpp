//===------------Instrumentation.cpp - TransformUtils Infrastructure ------===//
//
// This file is to provide some instrumentation utilities.
// These utilities usually come from the subsequent versions of LLVM, i.e., LLVM 15+.
//
//===----------------------------------------------------------------------===//
//
// This file defines the common initialization infrastructure for the
// Instrumentation library.
//
//===----------------------------------------------------------------------===//

#include "Instrumentation.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool> ClIgnoreRedundantInstrumentation(
    "ignore-redundant-instrumentation",
    cl::desc("Ignore redundant instrumentation"), cl::Hidden, cl::init(false));
namespace {
/// Diagnostic information for IR instrumentation reporting.
class DiagnosticInfoInstrumentation : public DiagnosticInfo {
  const Twine &Msg;

public:
  DiagnosticInfoInstrumentation(const Twine &DiagMsg,
                                DiagnosticSeverity Severity = DS_Warning)
      : DiagnosticInfo(DK_Linker, Severity), Msg(DiagMsg) {}
  void print(DiagnosticPrinter &DP) const override { DP << Msg; }
};
} // namespace

namespace __xsan {
/// Check if module has flag attached, if not add the flag.
bool checkIfAlreadyInstrumented(Module &M, StringRef Flag) {
  if (!M.getModuleFlag(Flag)) {
    M.addModuleFlag(Module::ModFlagBehavior::Override, Flag, 1);
    return false;
  }
  if (ClIgnoreRedundantInstrumentation)
    return true;
  std::string diagInfo =
      "Redundant instrumentation detected, with module flag: " +
      std::string(Flag);
  M.getContext().diagnose(
      DiagnosticInfoInstrumentation(diagInfo, DiagnosticSeverity::DS_Warning));
  return true;
}

constexpr char kDelegateMDKind[] = "xsan.delegate";

static unsigned DelegateMDKindID = 0;

void MarkAsDelegatedToXsan(Instruction &I) {
  auto &Ctx = I.getContext();
  if (!DelegateMDKindID)
    DelegateMDKindID = Ctx.getMDKindID(kDelegateMDKind);
  MDNode *N = MDNode::get(Ctx, None);
  I.setMetadata(DelegateMDKindID, N);
}

bool IsDelegatedToXsan(const Instruction &I) {
  if (!DelegateMDKindID)
    return false;
  return I.hasMetadata(DelegateMDKindID);
}

bool ShouldSkip(const Instruction &I) {
  return I.hasMetadata(LLVMContext::MD_nosanitize) || IsDelegatedToXsan(I);
}

} // namespace __xsan