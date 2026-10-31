# Detection-capability regression (`compare-checks.sh`)

`compare-checks.sh` answers one question about the MAIR backend:

> Does a build with `MAIR=1` ever emit fewer sanitizer checks than a build
> without MAIR?

It is deliberately **not** a performance, size or runtime benchmark, and it does
not run `check-asan`/`check-tsan`. Each sample is compiled twice (`MAIR` unset
and `MAIR=1`) to LLVM IR, the emitted check calls are counted per function, and
the two sides are diffed. That keeps the check fast and stable enough to gate
backend changes (A/B cards, D2/D3 ablations) without touching the big suites.

## Usage

```bash
# Compare the ASan samples, print the table.
test-e2e/regression/compare-checks.sh --suite asan

# Write the table to a file, then gate on it.
test-e2e/regression/compare-checks.sh --suite all --out /tmp/checks.tsv --fail-on-missing

# Reuse a specific build tree (default: build-llvm22, then build).
test-e2e/regression/compare-checks.sh --suite tsan --build build-C2
```

| Option | Meaning |
|---|---|
| `--suite asan\|tsan\|all` | Which sample set to compare (default `asan`). |
| `--out FILE` | Write the TSV to `FILE` instead of stdout. |
| `--build DIR` | Build tree providing `xclang`/`xclang++`. Defaults to `build-llvm22`, then `build`; `MAIR_BUILD` is the environment equivalent. |
| `--fail-on-missing` | Exit 1 when a function is covered by fewer **bytes** under MAIR. This is the gate meant for other cards. |
| `--fail-on-call-count` | Exit 1 when a function emits fewer **calls** under MAIR. Kept for the literal "count calls" check; see below for why it is not the default gate. |

Samples are `test/asan/TestCases/mair-*.cpp` and `test/tsan/TestCases/mair-*.c`,
`mair-*.cpp`: the accent of MAIR-specific shapes that already live in the lit
suite. Compile flags mirror the lit tests (`-asan`/`-tsan`, `-O1`; ASan also
gets `-mllvm -asan-opt=false` on both sides so native ASan optimization cannot
skew one side only). `MAIR` is disabled with `env -u MAIR`, because
`isMAIROptEnabled()` tests variable **existence**; an empty value does not turn
MAIR off.

## Output

```
file  func  sanitizer  count_baseline  count_mair  delta
      guarded_baseline  guarded_mair  guarded_delta
```

* `count_*` — number of `__asan_report_*` / `__tsan_{read,write,...}` calls
  inside the function.
* `guarded_*` — bytes covered by those checks. Constant-width checks use their
  width (`__asan_report_load4` → 4). Variable forms use the size operand
  (`__asan_report_load_n(ptr, i64 12)` → 12; `__tsan_read_range(ptr, i64 12)`
  → 12; for ASan's `..._range(addr, size, exp)` the first numeric operand is the
  size). A symbolic size is credited as one byte, which can only make the gate
  stricter. Only the widest check on each **address** counts, and two checks on
  the same address count once; see below.
* `delta` / `guarded_delta` — MAIR minus baseline. `guarded_delta >= 0` means
  nothing is left unguarded.

Functions are matched by name across the two compilations; a function present on
one side only is compared against zero. Compiler-generated stubs
(`asan.module_ctor`) with no checks on either side are omitted. `mair-ignore.c`
is an LLVM IR snippet (`-x ir` in its RUN line), not C, and is skipped.

To compare addresses instead of SSA names, the script follows two definition
shapes inside each function: `%N = ptrtoint ptr %base` and
`%N = getelementptr i8, ptr %base, i64 K`. So `%0`, `%14` and
`%mair.observation.gep` all canonicalize to `%base+0` when they derive from the
same pointer, and `*p = *p + 1` (load plus store to one location) counts its
4 bytes once instead of twice. Addresses whose provenance cannot be resolved
stay separate, which only makes the gate stricter.

## Why the gate uses guarded bytes, not call counts

Merging is a real MAIR feature now (A2/C6 for ASan, B2 for TSan), so the number
of check **calls** legitimately goes down:

```
mair-merged-range.cpp   baseline 3 x __asan_report_load4             (12 bytes)
                        MAIR     2 x __asan_report_load_n(, i64 12)  (24 bytes)
```

The three 4-byte loads become one 12-byte observation; the range is not a power
of two, so it is probed at its first and last byte (two calls, both
`i64 12`). The two probes sit on different address expressions, so the script
credits 24 bytes for a 12-byte range: it over-credits, never under-credits, so a
merge cannot fake a pass. Call counts drop by one or more while the covered
bytes stay the same or grow. Gating on call counts would flag every merged
sample as a regression and hide the real signal. `--fail-on-call-count` exists
only to demonstrate that mechanism and is expected to return non-zero on the
merged samples; `--fail-on-missing` is the gate to use.

Consequently the baseline currently does contain negative `delta` rows while
every `guarded_delta` is `>= 0`. If a run ever shows `guarded_delta < 0`, MAIR is
dropping a check and that is the finding to chase. As a sanity check of the
gate itself, compiling the MAIR side with `-mllvm -asan-instrument-writes=false`
makes it report the missing store checks and exit 1.

## Exit codes

| Code | Meaning |
|---|---|
| `0` | Comparison finished; requested gate passed (or no gate was requested). |
| `1` | Gate failed, or every sample failed to compile. |
| `2` | Usage error, missing compiler, or no samples for the requested suite. |

A sample that fails to compile is reported on stderr and skipped; a summary
line with the sample/row count and the gate result goes to stderr.
