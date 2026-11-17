#!/usr/bin/env python3
"""Collect one uniform MAIR ablation table across several packages (D5).

Two steps, both driven by this file:

    # 1. build every package in the three tiers and measure it
    python3 summarize.py --run --results evaluation/results

    # 2. fold the per-package tables into the report table
    python3 summarize.py --results evaluation/results --out evaluation/results/summary.csv

`--run` compiles each package with evaluation/<pkg>/compile.sh for the three
modes of evaluation/activate_compile_flags.sh (asan-new = baseline, asan-mair-build
= MAIR graph only, asan-mair-opt = full MAIR) and records, per (package, mode):

    runtime_seconds      one fuzzer/interpreter run over a fixed corpus, averaged
    max_rss_kb           peak RSS of that run (/usr/bin/time -f %M)
    binary_bytes         size of the linked artefact
    text_bytes           text segment of the artefact
    instrumented_mops    sanitizer check sites in the objects (llvm-objdump -r)
    frontend/optimizer/backend/codegen/asan_pass/execute_compiler seconds
                         -ftime-trace phases summed over all translation units

This script only writes numbers. It does not judge whether MAIR helps; that is
the reader's call.
"""

import argparse
import csv
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_DIR = SCRIPT_DIR.parent
METRICS_DIR = REPO_DIR / "test-e2e" / "metrics"
sys.path.insert(0, str(SCRIPT_DIR))
from analyze_compile_time import TRACE_PHASES, sum_named_events_json  # noqa: E402

PACKAGES = ["re2", "sqlite3", "zlib", "lua", "libpng"]
MODES = ["asan-new", "asan-mair-build", "asan-mair-opt"]
# Same columns as the per-package tables, so summary.csv is their concatenation.
COLUMNS = [
    "package", "mode", "program", "binary_bytes", "text_bytes",
    "instrumented_mops", "runtime_seconds", "max_rss_kb",
    "frontend_s", "optimizer_s", "backend_s", "codegen_s",
    "asan_pass_s", "execute_compiler_s", "repeats", "exit_status",
]
# Column name -> -ftime-trace event name.
PHASE_COLUMNS = {
    "frontend_s": "Frontend",
    "optimizer_s": "Optimizer",
    "backend_s": "Backend",
    "codegen_s": "CodeGenPasses",
    "asan_pass_s": "Total AddressSanitizerPass",
    "execute_compiler_s": "ExecuteCompiler",
}

LLVM_BIN = os.environ.get("LLVM_BIN", "/usr/lib/llvm-22/bin")
BUILD_DIR = REPO_DIR / "build-llvm22"
TIME_BIN = "/usr/bin/time"


def log(message):
    print(message, file=sys.stderr, flush=True)


def build_env(mode):
    env = os.environ.copy()
    env["PATH"] = f"{BUILD_DIR}:{LLVM_BIN}:" + env.get("PATH", "")
    env["LLVM_BIN"] = LLVM_BIN
    env["mode"] = mode
    # compile-all.sh -t does the same; the traces are what feeds the phase table.
    env["CFLAGS"] = "-ftime-trace"
    env["CXXFLAGS"] = "-ftime-trace"
    for key in ("MAIR",):
        env.pop(key, None)
    return env


def check_fuzzer_driver():
    """evaluation/libAFL.a must provide main(); the stale copy does not."""
    archive = SCRIPT_DIR / "libAFL.a"
    if not archive.is_file():
        return "missing evaluation/libAFL.a (run evaluation/fetch-gen-driver.sh)"
    out = subprocess.run(["nm", str(archive)], capture_output=True, text=True)
    if " T main" not in out.stdout:
        return "evaluation/libAFL.a has no main (run evaluation/fetch-gen-driver.sh)"
    return None


def fetch(pkg_dir, results):
    repo = pkg_dir / "repo"
    if repo.is_dir() and any(repo.iterdir()):
        return None
    script = pkg_dir / "fetch.sh"
    if not script.is_file():
        return f"{pkg_dir.name}: no repo and no fetch.sh"
    log(f"fetch {pkg_dir.name}")
    log_path = results / "logs" / f"{pkg_dir.name}.fetch.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    with log_path.open("w") as out:
        done = subprocess.run(["bash", "fetch.sh"], cwd=str(pkg_dir),
                              env=build_env("raw"), stdout=out,
                              stderr=subprocess.STDOUT, timeout=1800)
    if done.returncode != 0:
        return f"{pkg_dir.name}: fetch.sh exit {done.returncode} (see {log_path})"
    return None


def collect_outputs(pkg_dir, pkg, mode, results):
    """Copy binaries, objects and time traces out of temp/ before the next mode."""
    temp = pkg_dir / "temp"
    bins = sorted((temp / ".bin").glob("*")) if (temp / ".bin").is_dir() else []
    if not bins and (temp / "lua").is_file():
        bins = [temp / "lua"]
    if not bins:
        return [], "no binary produced"
    bin_dir = results / "bins" / pkg / mode
    bin_dir.mkdir(parents=True, exist_ok=True)
    names = []
    for binary in bins:
        if binary.is_file():
            shutil.copy2(binary, bin_dir / binary.name)
            names.append(binary.name)
    obj_dir = results / "objects" / pkg / mode / "temp"
    obj_dir.mkdir(parents=True, exist_ok=True)
    for obj in temp.rglob("*.o"):
        target = obj_dir / obj.relative_to(temp)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(obj, target)
    trace_dir = results / "traces" / pkg / mode
    trace_dir.mkdir(parents=True, exist_ok=True)
    for trace in temp.rglob("*.json"):
        shutil.copy2(trace, trace_dir / trace.name)
    return names, None


def build_package(pkg, mode, results):
    pkg_dir = SCRIPT_DIR / pkg
    log(f"build {pkg} {mode}")
    env = build_env(mode)
    log_path = results / "logs" / f"{pkg}.{mode}.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.rmtree(pkg_dir / "temp", ignore_errors=True)
    with log_path.open("w") as out:
        done = subprocess.run(["bash", "compile.sh"], cwd=str(pkg_dir),
                              env=env, stdout=out, stderr=subprocess.STDOUT)
    if done.returncode != 0:
        return None, f"{pkg}/{mode}: compile.sh exit {done.returncode} (see {log_path})"
    names, err = collect_outputs(pkg_dir, pkg, mode, results)
    if err:
        return None, f"{pkg}/{mode}: {err}"
    return names, None


def phases(pkg, mode, results):
    trace_dir = results / "traces" / pkg / mode
    if not trace_dir.is_dir():
        return {}
    totals, counts = sum_named_events_json(trace_dir)
    return {column: totals.get(event, 0.0) / 1e6
            for column, event in PHASE_COLUMNS.items()
            if counts.get(event)}


def metric_script(name, args, label, results):
    """Run one test-e2e/metrics collector and read back its single value."""
    out_dir = Path(tempfile.mkdtemp(prefix="d5-metrics-"))
    env = os.environ.copy()
    env["LLVM_BIN"] = LLVM_BIN
    done = subprocess.run(
        ["bash", str(METRICS_DIR / name), "--label", label] + args
        + ["--out", str(out_dir)], env=env, capture_output=True, text=True)
    if done.returncode != 0:
        return "unsupported", done.stderr.strip()
    tables = list(out_dir.glob("*.tsv"))
    if not tables:
        return "unsupported", f"{name} wrote no table"
    row = tables[0].read_text().splitlines()
    values = row[1].split("\t") if len(row) > 1 else []
    return (values[1] if len(values) > 1 else "unsupported"), None


def corpus_for(pkg, results):
    """Return a directory (or list of files) of fixed inputs for one package."""
    candidates = {
        "re2": [SCRIPT_DIR / "re2" / "corpus" / "re2@re2_fuzzer"],
        "zlib": [SCRIPT_DIR / "zlib" / "corpus" / "zlib"],
        "libpng": [SCRIPT_DIR / "libpng" / "corpus"],
    }
    for candidate in candidates.get(pkg, []):
        if candidate.is_dir():
            files = [f for f in sorted(candidate.iterdir()) if f.is_file()]
            if files:
                return files[:12]
    return seeds_for(pkg, results)


def seeds_for(pkg, results):
    """Minimal self-made seeds, used when the package ships no corpus."""
    directory = results / "corpus" / pkg
    directory.mkdir(parents=True, exist_ok=True)
    if any(directory.iterdir()):
        return [f for f in sorted(directory.iterdir()) if f.is_file()]
    if pkg == "zlib":
        payloads = [b"a" * 4096, b"hello zlib " * 512, b"".join(
            bytes([i % 251]) for i in range(8192))]
        for index, payload in enumerate(payloads):
            import zlib
            (directory / f"seed-{index}.z").write_bytes(zlib.compress(payload))
    elif pkg == "lua":
        scripts = ["print('hello')",
                   "local s=0 for i=1,1e4 do s=s+i end print(s)",
                   "local t={a=1,b=2} t.c=3 for k,v in pairs(t) do print(k,v) end",
                   "local function f(x) return x*x end print(f(7))"]
        for index, script in enumerate(scripts):
            (directory / f"seed-{index}.lua").write_text(script)
    elif pkg == "sqlite3":
        queries = ["CREATE TABLE t(a);SELECT 1;",
                   "CREATE TABLE t(a);INSERT INTO t VALUES(1),(2);SELECT * FROM t;",
                   "SELECT abs(-1), length('abc'), random();"]
        for index, query in enumerate(queries):
            (directory / f"seed-{index}.sql").write_text(query)
    elif pkg == "libpng":
        import struct
        import zlib
        def chunk(tag, payload):
            body = tag + payload
            return struct.pack(">I", len(payload)) + body + struct.pack(
                ">I", zlib.crc32(body) & 0xffffffff)
        header = struct.pack(">IIBBBBB", 8, 8, 8, 2, 0, 0, 0)
        raw = b"".join(b"\x00" + bytes([x, x, x]) * 8 for x in range(8))
        png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
               + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
        (directory / "seed-0.png").write_bytes(png)
    return [f for f in sorted(directory.iterdir()) if f.is_file()]


def run_timed(command, log_path, timeout, env=None):
    """Run one command under /usr/bin/time; return (seconds, max_rss_kb, status)."""
    stamp = Path(tempfile.mkstemp(prefix="d5-time-")[1])
    try:
        with log_path.open("a") as out:
            done = subprocess.run(
                [TIME_BIN, "-f", "%e %M", "-o", str(stamp)] + command,
                stdout=out, stderr=subprocess.STDOUT, timeout=timeout, env=env)
        status = done.returncode
        text = stamp.read_text().strip().splitlines()
        if not text:
            return None, None, status
        elapsed, rss = text[-1].split()
        return float(elapsed), int(rss), status
    except subprocess.TimeoutExpired:
        return None, None, "timeout"
    except ValueError:
        return None, None, "parse-error"
    finally:
        stamp.unlink(missing_ok=True)


def first_failure(statuses):
    """Report the first non-zero exit so a broken measurement is not hidden."""
    for status in statuses:
        if status != 0:
            return status
    return statuses[0] if statuses else "no-run"


def measure(pkg, mode, binary, seeds, results, repeats):
    log_path = results / "logs" / f"{pkg}.{mode}.run.log"
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text("")
    env = os.environ.copy()
    env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    env.setdefault("TSAN_OPTIONS", "report_bugs=0")
    times, rsses, statuses = [], [], []
    # The AFL driver takes a list of input files, so one process can cover the
    # whole corpus; the lua interpreter instead takes one script per process.
    # A directory argument would be read as a single file of absurd length.
    if "@" in binary.name:
        batches = [[str(binary)] + [str(seed) for seed in seeds]]
    else:
        batches = [[str(binary), str(seed)] for seed in seeds]
    for command in batches:
        for _ in range(repeats):
            elapsed, rss, status = run_timed(command, log_path, 300, env)
            if elapsed is not None:
                times.append(elapsed)
                rsses.append(rss)
            statuses.append(status)
    if not times:
        return None, None, statuses[0] if statuses else "no-run"
    return sum(times) / len(times), max(rsses), first_failure(statuses)


def row_for(pkg, mode, binary, seeds, results, repeats):
    label = f"{pkg}/{mode}"
    binary_bytes, err_size = metric_script(
        "collect-size.sh", ["--binary", binary], label, results)
    instrumented, err_inst = metric_script(
        "collect-instrumentation.sh",
        ["--objects", results / "objects" / pkg / mode / "temp"],
        label, results)
    if err_size:
        log(f"note: {label}: {err_size}")
    if err_inst:
        log(f"note: {label}: {err_inst}")
    runtime, rss, status = measure(pkg, mode, binary, seeds, results, repeats)
    phase = phases(pkg, mode, results)
    row = {
        "package": pkg,
        "mode": mode,
        "program": binary.name,
        "binary_bytes": binary_bytes,
        "text_bytes": "unsupported",
        "instrumented_mops": instrumented,
        "runtime_seconds": "unsupported" if runtime is None else f"{runtime:.3f}",
        "max_rss_kb": "unsupported" if rss is None else rss,
        "repeats": repeats,
        "exit_status": status,
    }
    for column in PHASE_COLUMNS:
        value = phase.get(column)
        row[column] = "unsupported" if value is None else f"{value:.3f}"
    row["text_bytes"] = read_text_bytes(binary)
    return row


def read_text_bytes(binary):
    done = subprocess.run(["size", str(binary)], capture_output=True, text=True)
    lines = done.stdout.splitlines()
    if len(lines) < 2:
        return "unsupported"
    fields = lines[1].split()
    return fields[0] if len(fields) > 1 else "unsupported"


def write_table(path, rows):
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=COLUMNS, delimiter="\t",
                                extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def run(args):
    results = Path(args.results)
    results.mkdir(parents=True, exist_ok=True)
    problems = []
    driver_error = check_fuzzer_driver()
    if driver_error:
        log(f"error: {driver_error}")
        return 1
    run_log = results / "run-log.md"
    failures = []
    for pkg in args.packages:
        pkg_dir = SCRIPT_DIR / pkg
        error = fetch(pkg_dir, results)
        if error:
            failures.append(error)
            log(f"FAILED {error}")
            continue
        seeds = corpus_for(pkg, results)
        rows = []
        for mode in MODES:
            binaries = sorted((results / "bins" / pkg / mode).glob("*")) \
                if args.reuse_build else []
            if binaries:
                log(f"reuse build {pkg} {mode}")
            else:
                names, error = build_package(pkg, mode, results)
                if error:
                    failures.append(error)
                    log(f"FAILED {error}")
                    continue
                binaries = [results / "bins" / pkg / mode / name for name in names]
            if not binaries:
                failures.append(f"{pkg}/{mode}: no binary to measure")
                continue
            # One package can produce several artefacts (zlib also builds
            # minigzip); measure the fuzz target, which carries the '@'.
            fuzzers = [b for b in binaries if "@" in b.name]
            target = (fuzzers or binaries)[0]
            row = row_for(pkg, mode, target, seeds, results, args.repeat)
            rows.append(row)
            log(f"measured {pkg} {mode}: runtime={row['runtime_seconds']} "
                f"rss={row['max_rss_kb']} mops={row['instrumented_mops']}")
        if rows:
            write_table(results / f"{pkg}.tsv", rows)
    if failures:
        with run_log.open("a") as handle:
            handle.write("\n## run failures\n\n")
            for failure in failures:
                handle.write(f"- {failure}\n")
        log(f"{len(failures)} failure(s) recorded in {run_log}")
    return 0


def aggregate(results, out):
    rows = []
    for table in sorted(Path(results).glob("*.tsv")):
        with table.open() as handle:
            reader = csv.DictReader(handle, delimiter="\t")
            rows.extend(reader)
    if not rows:
        log(f"no package tables under {results}")
        return 1
    rows.sort(key=lambda r: (r["package"], r["mode"]))
    with Path(out).open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=COLUMNS, delimiter=",",
                                extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)
    log(f"wrote {out} ({len(rows)} rows)")
    return 0


def plot(results, out):
    """Optional figures; the CSV is complete without them."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        from matplotlib import pyplot as plt
    except Exception as error:  # noqa: BLE001 - reported, not swallowed
        log(f"skip figures: matplotlib unavailable ({error})")
        return 0
    rows = list(csv.DictReader(Path(out).open(), delimiter=","))
    packages = sorted({r["package"] for r in rows})
    for column, name in [("runtime_seconds", "runtime"),
                         ("instrumented_mops", "instrumented"),
                         ("binary_bytes", "binary")]:
        figure, axis = plt.subplots(figsize=(8, 4))
        width = 0.25
        for index, mode in enumerate(MODES):
            values = []
            for package in packages:
                match = [r for r in rows
                         if r["package"] == package and r["mode"] == mode]
                value = match[0][column] if match else "unsupported"
                try:
                    values.append(float(value))
                except ValueError:
                    values.append(0.0)
            positions = [i + (index - 1) * width for i in range(len(packages))]
            axis.bar(positions, values, width=width, label=mode)
        axis.set_xticks(range(len(packages)))
        axis.set_xticklabels(packages)
        axis.set_ylabel(name)
        axis.legend(fontsize=8)
        figure.tight_layout()
        target = Path(out).with_name(f"summary_{name}.png")
        figure.savefig(target, dpi=200)
        plt.close(figure)
        log(f"wrote {target}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--results", type=Path,
                        default=SCRIPT_DIR / "results",
                        help="directory holding per-package TSVs and raw outputs")
    parser.add_argument("--out", type=Path,
                        default=SCRIPT_DIR / "results" / "summary.csv",
                        help="summary CSV to write")
    parser.add_argument("--run", action="store_true",
                        help="build and measure the packages")
    parser.add_argument("--packages", nargs="+", default=PACKAGES)
    parser.add_argument("--repeat", type=int, default=3,
                        help="repeats per runtime measurement")
    parser.add_argument("--reuse-build", action="store_true",
                        help="measure binaries already under results/bins")
    parser.add_argument("--plot", action="store_true",
                        help="also draw summary_*.png (needs matplotlib)")
    args = parser.parse_args()
    if args.run:
        status = run(args)
        if status:
            return status
    status = aggregate(args.results, args.out)
    if status:
        return status
    if args.plot:
        return plot(args.results, args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
