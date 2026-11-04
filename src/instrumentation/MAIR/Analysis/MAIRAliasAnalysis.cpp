#include "MAIR/Analysis/MAIRAliasAnalysis.h"

#include "MAIR/MAIR.h"

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Function.h"

using namespace llvm;

namespace mair {

MAIRAliasInfo::MAIRAliasInfo(Function &F, FunctionAnalysisManager &Manager)
    : Func(&F) {
  AA = &Manager.getResult<AAManager>(F);
}

bool MAIRAliasInfo::mustAlias(const Value &A, const Value &B) const {
  if (&A == &B)
    return true;
  if (!isAvailable())
    return false;
  if (!A.getType()->isPointerTy() || !B.getType()->isPointerTy())
    return false;
  // A one-byte location at each address is the standard "same address"
  // question. Asking with the real access size would let two accesses that start
  // at different offsets of one object answer MustAlias, and those offsets are
  // exactly what the caller's merge arithmetic still has to reconcile.
  return AA->isMustAlias(&A, &B);
}

MAIRAliasAnalysis::Result MAIRAliasAnalysis::run(MAIRGraph &G,
                                                 MAIRGraphAnalysisManager &) {
  if (!G.isLive())
    report_fatal_error("MAIR MAIRAliasAnalysis cannot run on a consumed graph");
  if (!FAM)
    report_fatal_error(
        "MAIR MAIRAliasAnalysis requires a FunctionAnalysisManager bridge");
  if (Function *F = G.getMutableFunction())
    return MAIRAliasInfo(*F, *FAM);
  return MAIRAliasInfo();
}

AnalysisKey MAIRAliasAnalysis::Key;

} // namespace mair
