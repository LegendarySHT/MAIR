#include "../Instrumentation.h"
#include <llvm/Support/CommandLine.h>
namespace __xsan {

/// TODO: Add and move all options here.

namespace options {
using namespace llvm;

namespace opt {
/// The master switch for XSan's instrumentation.
extern const cl::opt<bool> ClOpt;

/// Whether to reduce the recurring checks.
extern const cl::opt<bool> ClReccReduce;
/// Whether to reduce the recurring checks for ASan.
extern const cl::opt<bool> ClReccReduceAsan;
/// Whether to reduce the recurring checks for TSan.
extern const cl::opt<bool> ClReccReduceTsan;

/// The level of loop optimization designated for XSan.
/// - no: No loop optimization.
/// - range: Only combine periodic checks to range check.
/// - period: Only combine periodic checks.
/// - invariant: Only relocate invariant checks.
/// - full: Enable all loop optimizations as above.
extern const cl::opt<LoopOptLeval> ClLoopOpt;

/// Whether to perform post-optimization.
extern const cl::opt<bool> ClPostOpt;

inline bool enableReccReduction() { return ClOpt && ClReccReduce; }

inline bool enableReccReductionAsan() {
  return enableReccReduction() && ClReccReduceAsan;
}
inline bool enableReccReductionTsan() {
  return enableReccReduction() && ClReccReduceTsan;
}

inline bool enablePostOpt() { return ClOpt && ClPostOpt; }

inline LoopOptLeval loopOptLevel() {
  return ClOpt ? ClLoopOpt : LoopOptLeval::NoOpt;
}
} // namespace opt
} // namespace options

} // namespace __xsan