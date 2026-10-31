#pragma once

#include "MAIR/MAIRPasses.h"
#include "MAIR/Materialization/MAIRMaterialization.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Error.h"

#include <cassert>
#include <cstdlib>
#include <memory>

namespace mair {

/// How much of the MAIR pipeline the sanitizer should run.
///
/// `MAIR=0` (or an empty value) turns MAIR off, `MAIR=build` builds and
/// materializes the graph without running any graph transform, and `MAIR=1`
/// (or any other non-empty value) runs the full pipeline. BuildOnly is what
/// makes an ablation possible: it measures the framework's cost separately
/// from the optimization's benefit.
enum class Level { Off, BuildOnly, Optimize };

inline Level getMAIRLevel() {
  const char *Value = std::getenv("MAIR");
  if (!Value || Value[0] == '\0')
    return Level::Off;
  if (llvm::StringRef(Value) == "0")
    return Level::Off;
  if (llvm::StringRef(Value) == "build")
    return Level::BuildOnly;
  return Level::Optimize;
}

/// Whether sanitizer-side MAIR is engaged at all.
inline bool isMAIROptEnabled() { return getMAIRLevel() != Level::Off; }

/// Owns the complete MAIR lifecycle for one mutable LLVM function.
///
/// The barrier policy is a type because it is part of graph construction, not
/// a run-time optimization choice. Keeping the graph and its pass managers in
/// this object also makes the program-point-to-observation view valid until
/// instantiate() consumes the graph.
template <class BarrierTrait = DefaultBarrierTrait> class MAOpt {
public:
  using ProgramPointObservations =
      llvm::DenseMap<const ProgramPoint *,
                     llvm::SmallVector<const Observation *, 4>>;

  MAOpt(llvm::Function &F, llvm::FunctionAnalysisManager &FAM,
        llvm::PassInstrumentationCallbacks *PIC, bool KeepFullDebug,
        MAIROptimizationConfig Config = MAIROptimizationConfig::generic(),
        Level PipelineLevel = Level::Optimize)
      : F(F), FAM(FAM), KeepFullDebug(KeepFullDebug), Config(Config),
        PipelineLevel(PipelineLevel) {
    registerMAIRGraphAnalyses(GAM, PIC, &FAM);
  }

  MAOpt(const MAOpt &) = delete;
  MAOpt &operator=(const MAOpt &) = delete;

  MAIRGraph &build() {
    assert(!Graph && "MAOpt graph has already been built");
    // Skipping before normalization matters: normalizeMAIRLoops rewrites the
    // CFG, and a function that MAIR refuses has to come out untouched so the
    // legacy sanitizer sees exactly the IR it would have seen without MAIR.
    if (MAIRGraph::hasUnrepresentableCycle(F, FAM)) {
      LLVM_DEBUG(dbgs() << "MAIR: skipping " << F.getName()
                        << " (irreducible or unrepresented cycle)\n");
      Skipped = true;
      Graph = std::make_unique<MAIRGraph>(F, Config);
      return *Graph;
    }
    ModifiedIR = normalizeMAIRLoops(F, FAM);
    Graph = std::make_unique<MAIRGraph>(
        MAIRGraph::template build<BarrierTrait>(F, FAM, Config));
    rebuildProgramPointObservations();
    return *Graph;
  }

  void retainObservations(
      const llvm::SmallPtrSetImpl<const Observation *> &Targets) {
    assert(Graph && "MAOpt graph has not been built");

    for (ProgramPoint *Point : Graph->allProgramPoints()) {
      ProgramPoint &PP = *Point;
      for (auto It = PP.begin(); It != PP.end();) {
        if (Targets.contains(&*It))
          ++It;
        else
          It = PP.erase(It);
      }
    }
    rebuildProgramPointObservations();
  }

  llvm::Error optimize() {
    if (Skipped)
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "MAIR skipped a function with irreducible or unrepresented cycles");
    if (!Graph)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "MAOpt graph has not been built");
    if (Optimized)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "MAOpt graph has already been optimized");

    // BuildOnly materializes the graph exactly as constructed: no pass runs,
    // so every access keeps its own observation. That is the ablation level
    // that isolates MAIR's framework cost from its transformations.
    if (PipelineLevel == Level::BuildOnly) {
      Optimized = true;
      rebuildProgramPointObservations();
      return llvm::Error::success();
    }

    if (KeepFullDebug)
      buildMAIRGraphOgPipeline(PM);
    else
      buildMAIRGraphO1Pipeline(PM);
    PM.run(*Graph, GAM);
    if (!Graph->verify())
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "MAOpt produced an invalid MAIRGraph");
    for (MAIRGraph *Scope : Graph->graphs())
      GAM.clear(*Scope, F.getName());
    Optimized = true;
    rebuildProgramPointObservations();
    return llvm::Error::success();
  }

  llvm::Expected<MAIRInstantiationResult>
  instantiate(MAIRObservationBackend &Backend) {
    if (Skipped)
      return llvm::createStringError(
          llvm::inconvertibleErrorCode(),
          "MAIR skipped a function with irreducible or unrepresented cycles");
    if (!Graph)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "MAOpt graph has not been built");
    if (!Optimized)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "MAOpt graph has not been optimized");
    llvm::Expected<MAIRInstantiationResult> Result =
        Graph->instantiate(FAM, Backend);
    if (Result)
      ProgramPointToObservations.clear();
    return Result;
  }

  MAIRGraph &getGraph() {
    assert(Graph && "MAOpt graph has not been built");
    return *Graph;
  }
  const MAIRGraph &getGraph() const {
    assert(Graph && "MAOpt graph has not been built");
    return *Graph;
  }

  const ProgramPointObservations &getProgramPointObservations() const {
    assert(Graph && "MAOpt graph has not been built");
    return ProgramPointToObservations;
  }

  bool hasModifiedIR() const { return ModifiedIR; }

  llvm::ArrayRef<const Observation *>
  getProgramPointObservations(const ProgramPoint &PP) const {
    assert(Graph && "MAOpt graph has not been built");
    const auto It = ProgramPointToObservations.find(&PP);
    if (It == ProgramPointToObservations.end())
      return {};
    return It->second;
  }

private:
  void rebuildProgramPointObservations() {
    ProgramPointToObservations.clear();
    for (const ProgramPoint *Point : Graph->allProgramPoints()) {
      const ProgramPoint &PP = *Point;
      auto &Observations = ProgramPointToObservations[&PP];
      for (const Observation &Obs : PP)
        Observations.push_back(&Obs);
    }
  }

  llvm::Function &F;
  llvm::FunctionAnalysisManager &FAM;
  bool KeepFullDebug;
  MAIROptimizationConfig Config;
  bool Optimized = false;
  bool Skipped = false;
  Level PipelineLevel = Level::Optimize;
  bool ModifiedIR = false;
  std::unique_ptr<MAIRGraph> Graph;
  MAIRGraphAnalysisManager GAM;
  MAIRGraphPassManager PM;
  ProgramPointObservations ProgramPointToObservations;
};

} // namespace mair
