#include "MOP/AsanRedundancyAdapter.h"

#ifdef MOP_IR_USE_LLVM

#include "MOP/MOPData.h"
#include "MOP/MOPIRUnit.h"
#include "MOP/Passes/RedundantCheckEliminationPass.h"
#include "LLVM/LLVMPassContext.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/Instructions.h"

namespace MopIRImpl {
namespace MOP {

bool runAsanRedundancyReduction(llvm::Function &F,
                                llvm::FunctionAnalysisManager &FAM,
                                llvm::ModuleAnalysisManager &MAM,
                                llvm::ArrayRef<const llvm::Instruction *> TmpInsts,
                                llvm::SmallVectorImpl<const llvm::Instruction *> &Out) {
  Out.clear();

  if (TmpInsts.size() < 2) {
    Out.append(TmpInsts.begin(), TmpInsts.end());
    return true;
  }

  MOPIRUnit Unit(F.getName().str());
  for (const llvm::Instruction *I : TmpInsts) {
    if (!I)
      return false;
    llvm::MemoryLocation Loc = llvm::MemoryLocation::get(I);
    MopType Ty = llvm::isa<llvm::LoadInst>(I) ? MopType::Load : MopType::Store;
    Unit.addMop(std::make_unique<Mop>(Ty, I, Loc));
  }

  if (!Unit.isValid())
    return false;

  LLVM::LLVMPassContext Ctx(FAM, MAM, F);
  RedundantCheckEliminationPass Elim;
  Elim.optimize(Unit, Ctx);

  for (const auto &UP : Unit.getMops()) {
    if (!UP->isRedundant())
      Out.push_back(UP->getInstruction());
  }

  if (Out.empty())
    return false;
  return true;
}

} // namespace MOP
} // namespace MopIRImpl

#endif // MOP_IR_USE_LLVM
