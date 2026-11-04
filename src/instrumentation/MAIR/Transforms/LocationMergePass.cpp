#include "MAIR/Transforms/LocationMergePass.h"
#include "MAIR/Analysis/MAIRAliasAnalysis.h"
#include "MAIR/Analysis/MAIRStatAnalysis.h"
#include "MAIR/MAIR.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"

#include <functional>
#include <iterator>

using namespace llvm;

#define DEBUG_TYPE "mair-locationmerge"

STATISTIC(NumMergedObservations,
          "Number of observations merged by location merging");
STATISTIC(NumInputObservations,
          "Number of observations entering location merging");
STATISTIC(NumRebasedObservations,
          "Number of observations re-expressed against an equivalent base");

namespace mair {

namespace {

using ObservationIterator = ProgramPoint::iterator;

enum class MergeClass : uint8_t {
  ConcreteContiguous,
  NonConcreteContiguous,
  Masked,
  Periodic,
};

MergeClass getMergeClass(const Observation &Obs) {
  if (Obs.hasConcreteRange())
    return MergeClass::ConcreteContiguous;
  if (Obs.isContiguous())
    return MergeClass::NonConcreteContiguous;
  if (Obs.getMaskedRegion())
    return MergeClass::Masked;
  return MergeClass::Periodic;
}

const AbstractAccess *getLocationBounds(const Observation &Obs) {
  if (const AbstractAccess *Region = Obs.getContiguousRegion())
    return Region;
  if (const MaskedRegion *Region = Obs.getMaskedRegion())
    return &Region->getBounds();
  return nullptr;
}

bool compareAbstractValue(const AbstractValue &LHS, const AbstractValue &RHS) {
  if (LHS.isConstant() != RHS.isConstant())
    return LHS.isConstant();
  if (LHS.isConstant())
    return LHS.getConstant() < RHS.getConstant();
  if (LHS.isSymbol() != RHS.isSymbol())
    return LHS.isSymbol();
  if (LHS.isSymbol())
    return std::less<const Value *>()(LHS.getSymbol(), RHS.getSymbol());
  return false;
}

bool compareForLocationMerge(const Observation &LHS, const Observation &RHS) {
  const MemoryObject *LObject = LHS.getBaseUse().get();
  const MemoryObject *RObject = RHS.getBaseUse().get();
  if (LObject != RObject)
    return std::less<const MemoryObject *>()(LObject, RObject);
  if (LHS.isMergeable() != RHS.isMergeable())
    return LHS.isMergeable();
  if (LHS.getLevel() != RHS.getLevel())
    return LHS.getLevel() < RHS.getLevel();

  MergeClass LClass = getMergeClass(LHS);
  MergeClass RClass = getMergeClass(RHS);
  if (LClass != RClass)
    return LClass < RClass;

  const AbstractAccess *LBounds = getLocationBounds(LHS);
  const AbstractAccess *RBounds = getLocationBounds(RHS);
  if (!LBounds || !RBounds)
    return false;
  if (LBounds->getOffset() != RBounds->getOffset())
    return compareAbstractValue(LBounds->getOffset(), RBounds->getOffset());
  if (LBounds->getSize() == RBounds->getSize())
    return false;
  if (LBounds->getSize().isConstant() && RBounds->getSize().isConstant())
    return LBounds->getSize().getConstant() > RBounds->getSize().getConstant();
  return compareAbstractValue(LBounds->getSize(), RBounds->getSize());
}

bool hasSameMergeClass(const Observation &LHS, const Observation &RHS) {
  return LHS.getBaseUse().get() == RHS.getBaseUse().get() &&
         LHS.isMergeable() && RHS.isMergeable() &&
         LHS.getLevel() == RHS.getLevel() &&
         getMergeClass(LHS) == getMergeClass(RHS);
}

bool mergeConcreteRange(ProgramPoint &PP, ObservationIterator Begin,
                        ObservationIterator End) {
  bool Changed = false;
  if (Begin == End)
    return false;

  ObservationIterator Current = Begin;
  ObservationIterator Next = std::next(Current);
  while (Next != End) {
    if (!Current->mergeWith(*Next)) {
      Current = Next;
      Next = std::next(Current);
      continue;
    }
    Next = PP.erase(Next);
    Changed = true;
    ++NumMergedObservations;
  }
  return Changed;
}

bool mergeNonConcreteRange(ProgramPoint &PP, ObservationIterator Begin,
                           ObservationIterator End) {
  bool Changed = false;

  for (ObservationIterator Current = Begin; Current != End; ++Current) {
    for (ObservationIterator Next = std::next(Current); Next != End;) {
      if (!Current->mergeWith(*Next)) {
        ++Next;
        continue;
      }
      PP.erase(Next);
      Next = std::next(Current);
      Changed = true;
      ++NumMergedObservations;
    }
  }
  return Changed;
}

/// Whether every point that can name \p Original can name \p Replacement too.
///
/// The program point already guarantees that the base it holds can be
/// materialized there, so a substitute is only interchangeable when it is at
/// least as available; otherwise a later pass could be handed an observation
/// whose base is defined below the point.
bool isAvailableNoLaterThan(const DominatorTree &DT, const Value &Replacement,
                            const Value &Original) {
  // Arguments and constants are nameable everywhere, so a replacement built
  // from one is always at least as available as what it replaces.
  if (isa<Argument>(&Replacement) || isa<Constant>(&Replacement))
    return true;
  if (isa<Argument>(&Original) || isa<Constant>(&Original))
    return false;
  const auto *ReplacementInst = dyn_cast<Instruction>(&Replacement);
  const auto *OriginalInst = dyn_cast<Instruction>(&Original);
  if (!ReplacementInst || !OriginalInst)
    return false;
  return DT.dominates(ReplacementInst, OriginalInst);
}

/// Give the observations of one program point one base per address.
///
/// Merging requires two observations to name the same memory object, and the
/// graph interns exactly one object per base Value. An address can still be
/// written down with two Values -- a phi that picks the same pointer on every
/// incoming edge, for example -- so the observations are re-expressed against a
/// single base whenever AliasAnalysis proves the two are the same address. That
/// is the only evidence accepted here: an overlapping but different region
/// (PartialAlias) or a maybe (MayAlias) would let the merged check cover bytes
/// no source access ever touched.
///
/// Rewriting the base here rather than teaching every comparison about alias
/// analysis keeps the rest of the pass -- the sort, the run grouping, the merge
/// arithmetic -- working on one key, exactly as it did before.
bool canonicalizeBases(MAIRGraph &G, ProgramPoint &PP,
                       const MAIRAliasInfo &Aliases) {
  const DominatorTree &DT = G.getDominatorTree();

  // One representative per address seen so far, in program-point order.
  SmallVector<const Value *, 4> Representatives;
  bool Changed = false;

  for (Observation &Obs : PP) {
    const AbstractAccess *Region = Obs.getContiguousRegion();
    if (!Region || !Region->getBase())
      continue;
    const Value &Base = *Region->getBase();

    const Value *Representative = nullptr;
    for (const Value *Candidate : Representatives) {
      if (Aliases.mustAlias(*Candidate, Base)) {
        Representative = Candidate;
        break;
      }
    }

    if (!Representative) {
      Representatives.push_back(&Base);
      continue;
    }

    if (isAvailableNoLaterThan(DT, *Representative, Base)) {
      if (G.rebaseObservation(Obs, *Representative)) {
        ++NumRebasedObservations;
        Changed = true;
      }
      continue;
    }

    if (!isAvailableNoLaterThan(DT, Base, *Representative)) {
      // Neither name is available everywhere the other is, so they stay apart
      // even though they denote the same address.
      Representatives.push_back(&Base);
      continue;
    }

    // The base just seen is the one defined earlier: move the group over to it,
    // so later merges in this point see the most available name.
    for (const Value *&Candidate : Representatives)
      if (Candidate == Representative)
        Candidate = &Base;
    for (Observation &Other : PP) {
      const AbstractAccess *OtherRegion = Other.getContiguousRegion();
      if (OtherRegion && OtherRegion->getBase() == Representative &&
          G.rebaseObservation(Other, Base)) {
        ++NumRebasedObservations;
        Changed = true;
      }
    }
  }
  return Changed;
}

bool mergeLocations(ProgramPoint &PP) {
  PP.sort(compareForLocationMerge);

  bool Changed = false;
  for (ObservationIterator Begin = PP.begin(); Begin != PP.end();) {
    if (!Begin->isMergeable() || Begin->isPeriodic()) {
      ++Begin;
      continue;
    }

    ObservationIterator End = std::next(Begin);
    while (End != PP.end() && hasSameMergeClass(*Begin, *End))
      ++End;

    if (Begin->hasConcreteRange())
      Changed |= mergeConcreteRange(PP, Begin, End);
    else
      Changed |= mergeNonConcreteRange(PP, Begin, End);
    Begin = End;
  }
  return Changed;
}

} // namespace

PreservedAnalyses LocationMergePass::run(MAIRGraph &G,
                                         MAIRGraphAnalysisManager &AM) {
#if LLVM_ENABLE_STATS
  NumInputObservations += MAIRStat::compute(G).getNumObservations();
#endif
  const MAIRAliasInfo &Aliases = AM.getResult<MAIRAliasAnalysis>(G);

  bool Changed = false;
  for (ProgramPoint &PP : G) {
    if (Aliases.isAvailable())
      Changed |= canonicalizeBases(G, PP, Aliases);
    Changed |= mergeLocations(PP);
  }
  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // namespace mair
