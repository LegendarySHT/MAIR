#pragma once

#include <llvm/ADT/Optional.h>
#include <llvm/IR/Argument.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Value.h>

namespace __xsan {

// ---------------------- Helper Definitions -------------------------

// If the type has an associated member 'Name', return true; Otherwise, return
// false.
template <typename MetaT, typename = void>
struct IsMetaData : std::false_type {};
template <typename MetaT>
struct IsMetaData<MetaT, std::void_t<decltype(MetaT::Name)>> : std::true_type {
};

template <typename ExtraT> struct MetaDataExtra {
  using Extra = ExtraT;
  static llvm::MDNode *pack(llvm::LLVMContext &Ctx, const ExtraT &Data);
  static ExtraT unpack(llvm::MDNode *MD);
};

// If the type has an associated type 'Extra' and is a subclass of
// MetaDataExtra<Extra>, return true; Otherwise, return false.
template <typename MetaT, typename = void>
struct HasMetaDataExtra : std::false_type {};
template <typename MetaT>
struct HasMetaDataExtra<MetaT, std::void_t<typename MetaT::Extra>>
    : std::bool_constant<
          std::is_base_of_v<MetaDataExtra<typename MetaT::Extra>, MetaT>> {};

/// MetaT is only used to distinguish between different static members and does
/// not participate in the actual implementation
template <typename MetaT, typename InstT = llvm::Instruction>
class MetaDataHelperBase {
public:
  static bool is(const InstT &I);

protected:
  static unsigned ID;
};

template <typename MetaT, typename InstT = llvm::Instruction,
          bool HasExtra = HasMetaDataExtra<MetaT>::value,
          typename = std::enable_if_t<IsMetaData<MetaT>::value>>
class MetaDataHelper;

template <typename MetaT, typename InstT>
class MetaDataHelper<MetaT, InstT, false>
    : public MetaDataHelperBase<MetaT, InstT> {
  using MetaDataHelperBase<MetaT, InstT>::ID;

public:
  using MetaDataHelperBase<MetaT, InstT>::is;

  static void set(InstT &I);
};

template <typename MetaT, typename InstT>
class MetaDataHelper<MetaT, InstT, true>
    : public MetaDataHelperBase<MetaT, InstT> {
  using MetaDataHelperBase<MetaT, InstT>::ID;

public:
  using MetaDataHelperBase<MetaT, InstT>::is;
  using Extra = typename MetaT::Extra;

  static llvm::Optional<Extra> get(const InstT &I);
  static void set(InstT &I, const Extra &Data);
  static llvm::MDNode *getMD(const InstT &I);
  static void setMD(InstT &I, llvm::MDNode *MD);
};

// ---------------------- Delegated to XSan -------------------------

struct DelegateToXSanMeta {
  static constexpr char Name[] = "xsan.delegate";
};

// ---------------------- Replaced Alloca -------------------------

struct ReplacedAllocaExtra {
  size_t Len;
  llvm::Align Align;
  llvm::Argument *Arg; // valid for replaced argument
};

struct ReplacedAllocaMeta : MetaDataExtra<ReplacedAllocaExtra> {
  static constexpr char Name[] = "replaced.alloca";
};

// ---------------------- Copy Args -------------------------

struct CopyArgsMeta {
  static constexpr char Name[] = "copy.args";
};

// ---------------------- Replaced Atomic -------------------------

struct ReplacedAtomicExtra {
  enum AtomicType { Load, Store, RMW, CAS };

  AtomicType Type;
  llvm::Value *Addr;
  llvm::Value *Val; // is written val for store/rmw, is compare val for cas
  llvm::MaybeAlign Align; // valid for load/store
};

struct ReplacedAtomicMeta : MetaDataExtra<ReplacedAtomicExtra> {
  static constexpr char Name[] = "replaced.atomic";
};

// ---------------------- UBSan Instrumented -------------------------

struct UBSanInstMeta {
  static constexpr char Name[] = "xsan.ubsan";
};

// --------------- MetaDataHelper Specializations --------------------

using DelegateToXSan = MetaDataHelper<DelegateToXSanMeta>;
using ReplacedAlloca = MetaDataHelper<ReplacedAllocaMeta>;
using CopyArgs = MetaDataHelper<CopyArgsMeta, llvm::MemCpyInst>;
using ReplacedAtomic = MetaDataHelper<ReplacedAtomicMeta>;
using UBSanInst = MetaDataHelper<UBSanInstMeta>;

} // namespace __xsan
