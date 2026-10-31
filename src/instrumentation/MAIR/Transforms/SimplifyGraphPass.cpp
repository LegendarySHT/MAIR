#include "MAIR/Transforms/SimplifyGraphPass.h"
#include "MAIR/Analysis/MAIRStatAnalysis.h"
#include "MAIR/MAIR.h"

#include "llvm/ADT/Statistic.h"
using namespace llvm;

#define DEBUG_TYPE "mair-simplifygraph"

STATISTIC(NumInputProgramPoints,
          "Number of program points entering graph simplification");
STATISTIC(NumMergedProgramPoints,
          "Number of program points merged by graph simplification");

namespace mair {

namespace {

struct MergeCandidate {
  ProgramPoint *Into = nullptr;
  ProgramPoint *From = nullptr;
};

bool hasPinnedObservation(const ProgramPoint &PP) {
  return llvm::any_of(PP,
                      [&](const Observation &Obs) { return !Obs.isMovable(); });
}

MergeCandidate findMergeCandidate(MAIRGraph &G) {
  const MAIROptimizationConfig &Config = G.getOptimizationConfig();
  for (ProgramPoint &PP1 : G) {
    if (isa<LoopProgramPoint>(PP1))
      continue;
    for (const ProgramPointEdge &Edge : PP1.children()) {
      if (!Edge.isDom())
        continue;

      ProgramPoint &PP2 = Edge.getNode();
      if (&PP1 == &PP2 || isa<LoopProgramPoint>(PP2))
        continue;
      if (!PP1.hasCompatibleMergeSemantics(PP2))
        continue;
      if (!PP2.hasChild(PP1, PPEdgeKind::PostDom))
        continue;
      const BasicBlock *PP1Block = PP1.getBasicBlock();
      const BasicBlock *PP2Block = PP2.getBasicBlock();
      if (PP1Block != PP2Block) {
        assert(PP1Block && PP2Block &&
               "compatible program points must use the same site category");
        // Same constraint as PostDom redundancy: a loop-carried pointer can
        // take a new address on the exit, as in `load %p` inside the loop
        // and `load %p` after it. Do not merge those program points.
        const LoopInfo &Loops = G.getLoopInfo();
        if (Loops.getLoopFor(PP1Block) != Loops.getLoopFor(PP2Block))
          continue;
      }
      // Sinking keeps an observation as late as the region both points share
      // allows, which is the default: contract into the post-dominating site.
      if (!hasPinnedObservation(PP1))
        return {&PP2, &PP1};
      // The earlier point holds an observation that must not move later, so the
      // only direction left is to hoist PP2's observations into PP1. Hoisting a
      // check is sound here because the edge tests above already guarantee
      // PP1 dom PP2 and PP2 postdom PP1: the two points run on exactly the same
      // executions, so a check moved from PP2 to PP1 cannot end up on a path
      // that never reaches the access it guards. It stays ahead of the access
      // too, which is the property AddressSanitizer pins its observations for.
      if (!Config.allowsHoist())
        continue;
      if (!llvm::all_of(PP2, [&](const Observation &Obs) {
            return Obs.isAvailableAt(PP1.getAnchor(), G.getDominatorTree());
          }))
        continue;
      return {&PP1, &PP2};
    }
  }
  return {};
}

} // namespace

PreservedAnalyses SimplifyGraphPass::run(MAIRGraph &G,
                                         MAIRGraphAnalysisManager &AM) {
#if LLVM_ENABLE_STATS
  NumInputProgramPoints += MAIRStat::compute(G).getNumProgramPoints();
#else
  (void)AM;
#endif
  bool Changed = false;

  // Contract one mutual Dom/PostDom pair at a time. Rewiring can expose a new
  // pair outside the previous neighborhood, so each contraction starts a fresh
  // topology scan and preserves deterministic list-order selection.
  while (true) {
    MergeCandidate Candidate = findMergeCandidate(G);
    if (!Candidate.Into)
      break;
    G.contractProgramPoint(*Candidate.Into, *Candidate.From);
    ++NumMergedProgramPoints;
    Changed = true;
  }

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace mair
