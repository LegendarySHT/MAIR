#pragma once

#include "MAIR/MAIRPasses.h"

#include "llvm/IR/PassManager.h"

namespace llvm {
class AAResults;
class Function;
class Value;
} // namespace llvm

namespace mair {

/// Answers "are these two pointers the same address?" for the optimizations that
/// would otherwise compare base Values by identity.
///
/// Only MustAlias is exposed. PartialAlias says the regions overlap without
/// saying how, and MayAlias says nothing at all; neither is enough to treat two
/// observations as one, because a merged observation is checked as a single
/// region and would then cover bytes the original accesses never touched.
class MAIRAliasInfo {
  llvm::Function *Func = nullptr;
  // AAResults is not const-queryable: alias() carries a query cache.
  llvm::AAResults *AA = nullptr;

public:
  MAIRAliasInfo() = default;
  MAIRAliasInfo(llvm::Function &F, llvm::FunctionAnalysisManager &FAM);

  bool isAvailable() const { return Func && AA; }

  /// True when AliasAnalysis proves \p A and \p B are the same address at every
  /// point where both are defined. Identical Values answer true without asking.
  bool mustAlias(const llvm::Value &A, const llvm::Value &B) const;
};

/// Bridges the function-level AAResults into the graph pipeline.
///
/// The graph transforms do not change IR, so the AAResults computed when this
/// analysis first runs stays valid for the whole MAIR optimization; a pass that
/// rewrites the graph invalidates this result, and it is simply rebuilt.
class MAIRAliasAnalysis : public llvm::AnalysisInfoMixin<MAIRAliasAnalysis> {
  friend llvm::AnalysisInfoMixin<MAIRAliasAnalysis>;
  static llvm::AnalysisKey Key;

  llvm::FunctionAnalysisManager *FAM = nullptr;

public:
  using Result = MAIRAliasInfo;

  explicit MAIRAliasAnalysis(llvm::FunctionAnalysisManager *FAM = nullptr)
      : FAM(FAM) {}

  Result run(MAIRGraph &G, MAIRGraphAnalysisManager &AM);
};

} // namespace mair
