#include "Options.h"

namespace __xsan {
namespace options {

namespace opt {
const cl::opt<bool> ClOpt("xsan-opt", cl::desc("Optimize instrumentation"),
                          cl::Hidden, cl::init(true));
const cl::opt<bool> ClReccReduce("xsan-reduce-rec",
                                 cl::desc("Reduce recurring checks"),
                                 cl::Hidden, cl::init(true));
const cl::opt<bool>
    ClReccReduceAsan("xsan-reduce-rec-asan",
                     cl::desc("Reduce recurring checks for ASan"), cl::Hidden,
                     cl::init(true));
const cl::opt<bool>
    ClReccReduceTsan("xsan-reduce-rec-tsan",
                     cl::desc("Reduce recurring checks for TSan"), cl::Hidden,
                     cl::init(true));

const cl::opt<LoopOptLeval> ClLoopOpt(
    "xsan-loop-opt", cl::desc("Loop optimization level for XSan"),
    cl::values(
        clEnumValN(LoopOptLeval::NoOpt, "no",
                   "Disable loop optimization for XSan"),
        clEnumValN(LoopOptLeval::CombineToRangeCheck, "range",
                   "Only combine periodic checks to range check for XSan"),
        clEnumValN(LoopOptLeval::CombinePeriodicChecks, "period",
                   "Only combine periodic checks for XSan"),
        clEnumValN(LoopOptLeval::RelocateInvariantChecks, "invariant",
                   "Only relocate invariant checks for XSan"),
        clEnumValN(LoopOptLeval::Full, "full",
                   "Enable all loop optimization for XSan")),
    cl::Hidden, cl::init(LoopOptLeval::Full));

const cl::opt<bool> ClPostOpt(
    "xsan-post-opt", cl::init(true),
    cl::desc("Whether to perform post-sanitziers optimizations for XSan"),
    cl::Hidden);

} // namespace opt
} // namespace options

} // namespace __xsan