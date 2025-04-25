
#include <clang/CodeGen/BackendUtil.h>
#include <clang/Basic/CodeGenOptions.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/Sanitizers.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/Lex/HeaderSearchOptions.h>

#include "utils/PatchHelper.h"
#include "llvm/IR/Module.h"

using namespace clang;
using namespace llvm;

void clang::EmitBackendOutput(DiagnosticsEngine &Diags,
                              const HeaderSearchOptions &HeaderOpts,
                              const CodeGenOptions &CGOpts,
                              const clang::TargetOptions &TOpts,
                              const LangOptions &LOpts, StringRef TDesc,
                              Module *M, BackendAction Action,
                              std::unique_ptr<raw_pwrite_stream> OS) {
  static auto RealFunc = getRealFuncAddr(&clang::EmitBackendOutput);
  static SanitizerMask HackedSanitizers =
      SanitizerKind::Memory | SanitizerKind::Address | SanitizerKind::Thread;
  LangOptions NewLangOpts = LOpts;
  if (::XsanEnabled) {
    NewLangOpts.Sanitize.clear(HackedSanitizers);
  }
  RealFunc(Diags, HeaderOpts, CGOpts, TOpts, NewLangOpts, TDesc, M, Action,
           std::move(OS));
}
