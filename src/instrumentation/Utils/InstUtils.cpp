#include "InstUtils.h"
#include "llvm/IR/Instructions.h"

using namespace __xsan;
using namespace llvm;

namespace __xsan {
constexpr char kDelegateMDKind[] = "xsan.delegate";

static unsigned DelegateMDKindID = 0;

void markAsDelegatedToXsan(Instruction &I) {
  if (isDelegatedToXsan(I))
    return;
  auto &Ctx = I.getContext();
  if (!DelegateMDKindID)
    DelegateMDKindID = Ctx.getMDKindID(kDelegateMDKind);
  MDNode *N = MDNode::get(Ctx, None);
  I.setMetadata(DelegateMDKindID, N);
}

bool isDelegatedToXsan(const Instruction &I) {
  if (!DelegateMDKindID)
    return false;
  return I.hasMetadata(DelegateMDKindID);
}

bool shouldSkip(const Instruction &I) {
  return isNoSanitize(I) || isDelegatedToXsan(I);
}

const llvm::Value *extractAddrFromLoadStoreInst(const llvm::Instruction &I) {
  if (auto *LI = dyn_cast<LoadInst>(&I)) {
    return LI->getPointerOperand();
  }
  if (auto *SI = dyn_cast<StoreInst>(&I)) {
    return SI->getPointerOperand();
  }
  return nullptr;
}

} // namespace __xsan