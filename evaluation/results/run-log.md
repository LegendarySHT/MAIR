# Five-package sweep (D5) — run log

Host: WSL2 Ubuntu-20.04, `~/mair` at `task/D5-five-package-sweep` (base `62ad1e98`).
Driver: `python3 summarize.py --run --results results` (build + measure), then
`python3 summarize.py --results results --out results/summary.csv` (fold TSVs).
Raw binaries, objects, traces and per-run stdout stay under `results/` and are
git-ignored; only `summary.csv`, the per-package `*.tsv` and this log are tracked.

## Status

All 15 combinations (5 packages × 3 tiers) built and ran:

| package | seeds used | executions per tier | exit_status |
|---|---|---|---|
| re2 | 5 files from `re2/corpus/re2@re2_fuzzer` | 5 × repeats=3 | 0 |
| sqlite3 | 3 self-made SQL files | 3 × repeats=3 | 0 |
| zlib | 12 files from `zlib/corpus/zlib` | 12 × repeats=3 | 0 |
| lua | 4 self-made scripts | 4 × repeats=3 | 0 |
| libpng | 12 of 143 files from `libpng/corpus` | 12 × repeats=3 | 0 |

No build or runtime failure was recorded. `results/*.run.log` contains no
sanitizer report.

## Measurement caveats

- `runtime_seconds` is one process per repeat: the AFL driver receives all seed
  files as arguments, lua receives one script per process. Every package lands
  at ~0.040 s, i.e. the number is dominated by process start-up (ASan
  initialisation) rather than by the workload. It is reported as measured,
  not normalised.
- `max_rss_kb` is `/usr/bin/time -f %M` for the same run.
- `instrumented_mops` and `binary_bytes`/`text_bytes` come from
  `test-e2e/metrics/collect-instrumentation.sh` (counts
  `__asan_*`/`__tsan_*`/`__msan_*` relocations in the object files with
  `llvm-objdump -r`) and `collect-size.sh`.
- Compile phases are the sum of `-ftime-trace` events over all translation
  units; MAIR's four internal stages do not appear as separate trace events.

## Environment gaps found on this machine

| gap | effect | workaround |
|---|---|---|
| no libc++ headers or libraries | `-stdlib=libc++` links fail (`cannot find -lc++`) | `activate_compile_flags.sh` probes once and exports `MAIR_LIBCXX_FLAG` (empty when missing); re2/zlib/libpng `compile.sh` use `${MAIR_LIBCXX_FLAG--stdlib=libc++}` |
| no autotools (`configure` absent) | libpng `./configure` unavailable | libpng builds through its own `CMakeLists.txt` (`PNG_SHARED=OFF`, `PNG_STATIC=ON`, `PNG_TESTS=OFF`, `make png_static`) |
| shipped `evaluation/libAFL.a` has no `main` | link error `undefined symbol: main` | rebuilt the archive from `afl_driver.cpp` (`evaluation/fetch-gen-driver.sh`); `summarize.py --run` checks `nm ... " T main"` first |
| zlib `make all` fails in `infcover` | `-coverage` needs `libclang_rt.profile.a`, not installed | `make zlibstatic` + minigzip taken from `test/minigzipstatic` |
| libpng checkout ships no oss-fuzz harness and `wget` of oss-fuzz times out | nothing to link | local stand-in `evaluation/libpng/libpng_read_fuzzer.cc` (same `LLVMFuzzerTestOneInput` signature, `png_image` read API) |
| absl not registered with `pkg-config` | re2 link misses absl symbols | re2 `compile.sh` falls back to `-Wl,--start-group absl-install/lib/*.a -Wl,--end-group` |
| no `matplotlib` | `--plot` produces no figures | prints `skip figures: matplotlib unavailable`; the CSV is unaffected |
| no PMU (`perf_event_paranoid=2`) | instruction/cycle counters unavailable | not part of this table (D4 recorded the same limitation) |

## Figures

The card asks for 2–3 figures. `--plot` was executed and was skipped:

```
skip figures: matplotlib unavailable (No module named 'matplotlib')
```

The CSV is complete without them; `summary_*.png` are not produced on this host.
