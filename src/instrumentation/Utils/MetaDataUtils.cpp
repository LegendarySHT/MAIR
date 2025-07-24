#include "MetaDataUtils.h"
#include "llvm/IR/Instruction.h"

using namespace llvm;

namespace __xsan {

// ---------------------- MetaDataHelper -------------------------

template <typename MetaT, typename InstT>
unsigned MetaDataHelperBase<MetaT, InstT>::ID = 0;

template <typename MetaT, typename InstT>
void MetaDataHelper<MetaT, InstT, false>::set(InstT &I) {
  // We cannot use func-local static var to init the Ctx, as it might differ for
  // different Modules.
  llvm::LLVMContext &Ctx = I.getContext();
  if (!ID)
    ID = Ctx.getMDKindID(MetaT::Name);
  if (is(I))
    return;
  I.setMetadata(ID, llvm::MDNode::get(Ctx, llvm::None));
}

template <typename MetaT, typename InstT>
auto MetaDataHelper<MetaT, InstT, true>::get(const InstT &I)
    -> llvm::Optional<Extra> {
  MDNode *MD;
  if (!ID || !(MD = I.getMetadata(ID)))
    return llvm::None;
  return MetaDataExtra<Extra>::unpack(MD);
}

template <typename MetaT, typename InstT>
void MetaDataHelper<MetaT, InstT, true>::set(InstT &I, const Extra &Data) {
  // We cannot use func-local static var to init the Ctx, as it might differ for
  // different Modules.
  llvm::LLVMContext &Ctx = I.getContext();
  if (!ID)
    ID = Ctx.getMDKindID(MetaT::Name);
  I.setMetadata(ID, MetaDataExtra<Extra>::pack(Ctx, Data));
}

template <typename MetaT, typename InstT>
llvm::MDNode *MetaDataHelper<MetaT, InstT, true>::getMD(const InstT &I) {
  return ID ? I.getMetadata(ID) : nullptr;
}

template <typename MetaT, typename InstT>
void MetaDataHelper<MetaT, InstT, true>::setMD(InstT &I, MDNode *MD) {
  if (ID)
    I.setMetadata(ID, MD);
}

// ---------------------- Replaced Alloca -------------------------

template <>
MDNode *
MetaDataExtra<ReplacedAllocaExtra>::pack(LLVMContext &Ctx,
                                         const ReplacedAllocaExtra &Data) {
  ConstantInt *LenConst = ConstantInt::get(Type::getInt64Ty(Ctx), Data.Len);
  ConstantInt *AlignConst =
      ConstantInt::get(Type::getInt64Ty(Ctx), Data.Align.value());

  Metadata *LenMetadata = ConstantAsMetadata::get(LenConst);
  Metadata *AlignMetadata = ConstantAsMetadata::get(AlignConst);
  // Metadata *ArgMetadata = Data.Arg ? ValueAsMetadata::get(Data.Arg) :
  // nullptr;
  return MDNode::get(Ctx, {LenMetadata, AlignMetadata});
}

template <>
ReplacedAllocaExtra MetaDataExtra<ReplacedAllocaExtra>::unpack(MDNode *MD) {
  ReplacedAllocaExtra Data;

  Metadata *LenMD = MD->getOperand(0).get();
  ConstantAsMetadata *LenConstMD = cast<ConstantAsMetadata>(LenMD);
  ConstantInt *LenConstInt = cast<ConstantInt>(LenConstMD->getValue());
  Data.Len = LenConstInt->getZExtValue();

  Metadata *AlignMD = MD->getOperand(1).get();
  ConstantAsMetadata *AlignConstMD = cast<ConstantAsMetadata>(AlignMD);
  ConstantInt *AlignConstInt = cast<ConstantInt>(AlignConstMD->getValue());
  Data.Align = AlignConstInt->getAlignValue();

  // Metadata *ArgMD = MD->getOperand(2).get();
  // if (auto *ValMD = ArgMD ? dyn_cast<ValueAsMetadata>(ArgMD) : nullptr)
  //   Data.Arg = cast<Argument>(ValMD->getValue());
  // else
  //   Data.Arg = nullptr;

  return Data;
}

// ---------------------- Replaced Atomic -------------------------

template <>
MDNode *
MetaDataExtra<ReplacedAtomicExtra>::pack(LLVMContext &Ctx,
                                         const ReplacedAtomicExtra &Data) {
  Metadata *TypeMetadata = ConstantAsMetadata::get(
      ConstantInt::get(Type::getInt32Ty(Ctx), static_cast<int>(Data.Type)));
  Metadata *AddrMetadata = ValueAsMetadata::get(Data.Addr);
  Metadata *ValMetadata = Data.Val ? ValueAsMetadata::get(Data.Val) : nullptr;
  Metadata *AlignMetadata = ConstantAsMetadata::get(ConstantInt::get(
      Type::getInt32Ty(Ctx), Data.Align ? Data.Align->value() : 0));

  return MDNode::get(Ctx,
                     {TypeMetadata, AddrMetadata, ValMetadata, AlignMetadata});
}

template <>
ReplacedAtomicExtra MetaDataExtra<ReplacedAtomicExtra>::unpack(MDNode *MD) {
  ReplacedAtomicExtra Data;

  Metadata *TypeMD = MD->getOperand(0).get();
  Data.Type = static_cast<ReplacedAtomicExtra::AtomicType>(
      cast<ConstantInt>(cast<ConstantAsMetadata>(TypeMD)->getValue())
          ->getZExtValue());

  Metadata *AddrMD = MD->getOperand(1).get();
  Data.Addr = cast<ValueAsMetadata>(AddrMD)->getValue();

  Metadata *ValMD = MD->getOperand(2).get();
  Data.Val = ValMD ? cast<ValueAsMetadata>(ValMD)->getValue() : nullptr;

  Metadata *AlignMD = MD->getOperand(3).get();
  Data.Align = cast<ConstantInt>(cast<ConstantAsMetadata>(AlignMD)->getValue())
                   ->getMaybeAlignValue();

  return Data;
}

// --------------- MetaDataHelper Specializations --------------------

/// Explicit Specialization
// template class MetaDataHelper<UBSanInstMeta>;
/// Explicit Declaration
// template<> class MetaDataHelper<UBSanInstMeta>;
#define INSTANTIATE_META_DATA_HELPER(...)                                      \
  template class MetaDataHelperBase<__VA_ARGS__>;                              \
  template class MetaDataHelper<__VA_ARGS__>;

INSTANTIATE_META_DATA_HELPER(DelegateToXSanMeta)
INSTANTIATE_META_DATA_HELPER(ReplacedAllocaMeta)
INSTANTIATE_META_DATA_HELPER(CopyArgsMeta, llvm::MemCpyInst)
INSTANTIATE_META_DATA_HELPER(ReplacedAtomicMeta)
INSTANTIATE_META_DATA_HELPER(UBSanInstMeta)

} // namespace __xsan
