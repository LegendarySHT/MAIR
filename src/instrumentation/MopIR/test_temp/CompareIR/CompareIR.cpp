// Compare MopRecurrenceReducer and MopIR MopRedundancyAnalysis on same IR

#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Analysis/DominanceFrontier.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemoryLocation.h"

#include "../../../Analysis/MopRecurrenceReducer.h"
#include "../../include/MopIR.h"
#include "../../include/MopAnalysis.h"

using namespace llvm;
using namespace __xsan;
using namespace __xsan::MopIR;

static void dumpInstList(StringRef Header,
                         ArrayRef<const Instruction*> List) {
  outs() << Header << " (" << List.size() << ")\n";
  for (const Instruction *I : List) {
    outs() << "    ";
    if (I)
      I->print(outs());
    outs() << "\n";
  }
}

static SmallVector<const Instruction*, 64> collectInstsFromMopIR(const __xsan::MopIR::MopIR &MIR) {
  SmallVector<const Instruction*, 64> Insts;
  for (const auto &UP : MIR.getMops()) {
    if (const Instruction *I = UP->getOriginalInst()) {
      Insts.push_back(I);
    }
  }
  return Insts;
}

static bool hasMemoryLocation(const Instruction *I) {
  auto Opt = MemoryLocation::getOrNone(I);
  return Opt.has_value();
}

int main(int argc, char **argv) {
  if (argc < 2) {
    errs() << "Usage: " << argv[0] << " <input.ll> [--tsan] [--ignore-calls]\n";
    return 1;
  }

  bool IsTsan = false;
  bool IgnoreCalls = false;
  for (int i = 2; i < argc; ++i) {
    StringRef A(argv[i]);
    if (A == "--tsan") IsTsan = true;
    else if (A == "--ignore-calls") IgnoreCalls = true;
  }

  LLVMContext Ctx;
  SMDiagnostic Err;
  std::unique_ptr<Module> M = parseIRFile(argv[1], Err, Ctx);
  if (!M) {
    Err.print("CompareIR", errs());
    return 1;
  }

  outs() << "=== Compare MopIR vs MopRecurrenceReducer ===\n";
  outs() << "Input: " << argv[1] << "\n";
  outs() << "Mode : " << (IsTsan ? "TSan" : "ASan")
         << ", IgnoreCalls=" << (IgnoreCalls?"true":"false") << "\n\n";

  bool AllOK = true;

  PassBuilder PB;
  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  // Ensure AA/DT/PDT/Loop/SE/TLI are available for both pipelines
  FAM.registerPass([&]() { return DominatorTreeAnalysis(); });
  FAM.registerPass([&]() { return PostDominatorTreeAnalysis(); });
  FAM.registerPass([&]() { return LoopAnalysis(); });
  FAM.registerPass([&]() { return ScalarEvolutionAnalysis(); });
  FAM.registerPass([&]() { return TargetLibraryAnalysis(); });
  FAM.registerPass([&]() { return AAManager(); });

  for (Function &F : *M) {
    if (F.isDeclaration()) continue;
    outs() << "Function: " << F.getName() << "\n";

    // Build MopIR MOPs
    __xsan::MopIR::MopIR MIR(F, FAM);
    MIR.build();

    // Prepare Reducer input from MopIR MOPs to ensure the same universe
    SmallVector<const Instruction*, 64> Insts = collectInstsFromMopIR(MIR);
    dumpInstList("  MopIR extracted insts", Insts);
    // Filter out instructions without a definable MemoryLocation to avoid
    // Optional<MemoryLocation> assertions inside the legacy reducer.
    SmallVector<const Instruction*, 64> FilteredInsts;
    FilteredInsts.reserve(Insts.size());
    DenseSet<const Instruction*> FilteredSet;
    for (const Instruction *I : Insts) {
      if (hasMemoryLocation(I)) {
        FilteredInsts.push_back(I);
        FilteredSet.insert(I);
      }
    }

    dumpInstList("  Filtered insts (with MemoryLocation)", FilteredInsts);

    if (FilteredInsts.size() != Insts.size()) {
      outs() << "  Skipped (function contains memory operations without definable MemoryLocation)\n\n";
      continue;
    }

    // Run Reducer
    MopRecurrenceReducer Reducer(F, FAM);
    SmallVector<const Instruction*, 16> Reduced =
        Reducer.distillRecurringChecks(FilteredInsts, IsTsan, IgnoreCalls);

    dumpInstList("  Reducer survivors", Reduced);

    // Run MopIR RedundancyAnalysis
    MopRedundancyAnalysis Redund;
    Redund.setContext(MIR.getContext());
    Redund.setTsanMode(IsTsan);
    Redund.setIgnoreCalls(IgnoreCalls);
    Redund.analyze(MIR.getMops());

    // Collect survivors from MopIR (non-redundant)
    DenseSet<const Instruction*> SurvivorsIR;
    for (const auto &UP : MIR.getMops()) {
      const Mop *MopPtr = UP.get();
      const Instruction *I = MopPtr->getOriginalInst();
      if (!I || !FilteredSet.contains(I)) continue;
      if (!Redund.isRedundant(MopPtr)) {
        SurvivorsIR.insert(I);
      }
    }

    SmallVector<const Instruction*, 64> SurvivorsIRList;
    for (const Instruction *I : SurvivorsIR) SurvivorsIRList.push_back(I);
    dumpInstList("  MopIR survivors", SurvivorsIRList);

    // Collect survivors from Reducer
    DenseSet<const Instruction*> SurvivorsRR;
    for (const Instruction *I : Reduced) {
      SurvivorsRR.insert(I);
    }

    // Compare
    bool Match = true;
    for (const Instruction *I : SurvivorsRR) {
      if (!SurvivorsIR.contains(I)) {
        Match = false;
        errs() << "  Reducer survivor missing in MopIR: "; I->print(errs()); errs() << "\n";
      }
    }
    for (const Instruction *I : SurvivorsIR) {
      if (!SurvivorsRR.contains(I)) {
        Match = false;
        errs() << "  MopIR survivor missing in Reducer: "; I->print(errs()); errs() << "\n";
      }
    }

    outs() << (Match ? "  ✓ Match\n\n" : "  ✗ Mismatch\n\n");
    AllOK &= Match;
  }

  return AllOK ? 0 : 2;
}
