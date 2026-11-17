#!/bin/bash
# Detection-capability regression: compile the MAIR-focused lit samples with
# MAIR off and MAIR on, then compare the sanitizer checks each function emits.
#
# This is not a performance or binary-size benchmark. It answers one question:
# does `MAIR=1` ever lose a check that a build without MAIR emits?
#
# Usage:
#   compare-checks.sh [--suite asan|tsan|all] [--out FILE] [--build DIR]
#                     [--fail-on-missing] [--fail-on-call-count]
#
# Output is TSV:
#   file  func  sanitizer  count_baseline  count_mair  delta
#         guarded_baseline  guarded_mair  guarded_delta
#
# count_* is the number of check calls. It may legitimately drop when MAIR
# merges contiguous accesses into one range check, so the gate compares
# guarded_* instead: the number of bytes the emitted checks cover, summing
# the widest check per distinct address (a load and a store to the same
# address guard the same bytes and count once).
#
set -euo pipefail
shopt -s nullglob

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
REPO_DIR=$(cd -- "$SCRIPT_DIR/../.." && pwd)

SUITE=asan
OUT=
BUILD_DIR=${MAIR_BUILD:-}
FAIL_ON_MISSING=0
FAIL_ON_CALL_COUNT=0

usage() {
  sed -n '8,14p' "$0" >&2
  exit 2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --suite)
      SUITE=${2:-}
      shift 2 || usage
      ;;
    --out)
      OUT=${2:-}
      shift 2 || usage
      ;;
    --build)
      BUILD_DIR=${2:-}
      shift 2 || usage
      ;;
    --fail-on-missing) FAIL_ON_MISSING=1; shift ;;
    --fail-on-call-count) FAIL_ON_CALL_COUNT=1; shift ;;
    -h|--help) usage ;;
    *)
      echo "unknown argument: $1" >&2
      usage
      ;;
  esac
done

case "$SUITE" in
  asan|tsan|all) ;;
  *)
    echo "unknown suite: $SUITE (expected asan, tsan or all)" >&2
    exit 2
    ;;
esac

if [[ -z "$BUILD_DIR" ]]; then
  for candidate in build-llvm22 build; do
    if [[ -x "$REPO_DIR/$candidate/xclang++" ]]; then
      BUILD_DIR=$REPO_DIR/$candidate
      break
    fi
  done
fi
if [[ ! -x "$BUILD_DIR/xclang++" || ! -x "$BUILD_DIR/xclang" ]]; then
  echo "xclang/xclang++ not found under '$BUILD_DIR'; pass --build DIR or set MAIR_BUILD" >&2
  exit 2
fi
CCXX=$BUILD_DIR/xclang++
CCC=$BUILD_DIR/xclang

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
REPORT=$WORK/report.tsv

# One record per function: "<name>\t<calls>\t<guarded bytes>".
#
# A check's width comes from the callee name (`__asan_report_load4`,
# `__tsan_read8`) or, for the variable-size forms, from the i64 constant in the
# call (`__asan_report_load_n(ptr, i64 12)`). A symbolic size counts as one
# guarded byte: under-crediting the baseline can only make the gate stricter.
#
# Guarded bytes are summed over *distinct addresses*, taking the widest check
# per address. Two checks can guard the same bytes: `*p = *p + 1` is instrumented
# once for the load and once for the store, and MAIR folds that pair into one
# wider ReadWrite observation, so counting the two calls as two separate
# accesses would report a loss that is not there. To compare addresses rather
# than SSA names, `%N = ptrtoint ptr %base` and `%N = getelementptr i8, ptr
# %base, i64 K` are tracked, so `%p`, `%gep` and `%0` all canonicalize to
# `%p+0`. Addresses that cannot be resolved stay separate, which only makes the
# gate stricter.
count_checks() { # $1: emitted .ll, $2: suite
  awk -v suite="$2" '
    function width_of(callee, line,   args, snippet, nconst, consts) {
      if (match(callee, /[0-9]+$/))
        return substr(callee, RSTART, RLENGTH) + 0
      nconst = 0
      if (callee ~ /(_n|_range)$/) {
        args = line
        sub(/.*@[A-Za-z0-9_]+\(/, "", args)
        while (match(args, /i64 -?[0-9]+/)) {
          snippet = substr(args, RSTART + 4, RLENGTH - 4)
          if (snippet ~ /^-?[0-9]+$/)
            consts[++nconst] = snippet + 0
          args = substr(args, RSTART + RLENGTH)
        }
      }
      if (callee ~ /_range$/) {
        # TSan passes (ptr, i64 size): the size is the only numeric operand.
        # ASan passes (i64 addr, i64 size, i64 exp): the size comes first.
        if (suite == "asan")
          return nconst >= 1 ? consts[1] : 1
        return nconst >= 1 ? consts[nconst] : 1
      }
      return nconst >= 1 ? consts[nconst] : 1
    }
    # "%0" from a definition line, "" otherwise.
    function def_name(line) {
      if (match(line, /^  %[A-Za-z0-9_.$]+ = /))
        return substr(line, 3, RLENGTH - 5)
      return ""
    }
    # "%p" or "@g" from "ptr OPERAND" in a definition line, "" otherwise.
    function def_operand(line, pat) {
      if (match(line, pat))
        return substr(line, RSTART + 4, RLENGTH - 4)
      return ""
    }
    # Canonical "<base> SUBSEP <offset>" for an SSA value or a literal.
    function canon(v) {
      if (v in base)
        return base[v] SUBSEP off[v]
      return v SUBSEP 0
    }
    # Remember address provenance so that two checks on the same location
    # compare equal even when they run through different SSA chains.
    function note_address_def(line,   v, s, kv) {
      if (line ~ /^  %[A-Za-z0-9_.$]+ = ptrtoint ptr /) {
        v = def_name(line)
        s = def_operand(line, "ptr [%@][A-Za-z0-9_.$@]+")
        if (v == "" || s == "")
          return 0
        split(canon(s), kv, SUBSEP)
        base[v] = kv[1]
        off[v] = kv[2] + 0
        return 1
      }
      if (line ~ /^  %[A-Za-z0-9_.$]+ = getelementptr i8, ptr /) {
        v = def_name(line)
        s = def_operand(line, "ptr [%@][A-Za-z0-9_.$@]+, i64 -?[0-9]+")
        if (v == "" || s == "")
          return 0
        sub(/, i64 /, SUBSEP, s)
        split(s, kv, SUBSEP)
        split(canon(kv[1]), kk, SUBSEP)
        base[v] = kk[1]
        off[v] = kk[2] + kv[2]
        return 1
      }
      return 0
    }
    /^define / {
      # Names are the last "@token" before the parameter list; attributes such
      # as `range(i32 0, 2)` carry parentheses too, so anchor on "@name(".
      if (match($0, /@[^@ \t(]+\(/))
        fn = substr($0, RSTART + 1, RLENGTH - 2)
      else
        fn = "@" NR
      gsub(/"/, "", fn)
      if (!(fn in seen)) {
        seen[fn] = 1
        order[++nfns] = fn
      }
      split("", base)
      split("", off)
      inbody = 1
      next
    }
    inbody && /^}/ { inbody = 0; next }
    inbody {
      if (note_address_def($0))
        next
      if ($0 !~ /(call|invoke)/)
        next
      callee = $0
      sub(/.*@/, "", callee)
      sub(/\(.*/, "", callee)
      if (suite == "asan" && callee !~ /^__asan_report_/)
        next
      if (suite == "tsan" && callee !~ /^__tsan_(read|write|unaligned|volatile|atomic)/)
        next
      calls[fn]++
      addr = $0
      sub(/^[^(]*\(/, "", addr)
      sub(/[,)].*$/, "", addr)
      gsub(/^[ \t]+|[ \t]+$/, "", addr)
      sub(/^[A-Za-z0-9*]+ /, "", addr)
      gsub(/"/, "", addr)
      w = width_of(callee, $0)
      key = fn SUBSEP canon(addr)
      if (w > width[key])
        width[key] = w
    }
    END {
      for (i = 1; i <= nfns; i++) {
        f = order[i]
        g = 0
        for (key in width) {
          split(key, kv, SUBSEP)
          if (kv[1] == f)
            g += width[key]
        }
        printf "%s\t%d\t%d\n", f, calls[f], g
      }
    }
  ' "$1"
}

# Join the baseline and MAIR records of one sample, keyed by function name.
merge_records() { # $1: baseline, $2: mair, $3: file label, $4: suite
  awk -F '\t' -v file="$3" -v suite="$4" '
    FNR == NR {
      order[++n] = $1
      seen[$1] = 1
      bc[$1] = $2
      bg[$1] = $3
      next
    }
    {
      f = $1
      if (!(f in seen)) {
        seen[f] = 1
        order[++n] = f
        bc[f] = 0
        bg[f] = 0
      }
      mc[f] = $2
      mg[f] = $3
    }
    END {
      for (i = 1; i <= n; i++) {
        f = order[i]
        mc[f] += 0
        mg[f] += 0
        # Compiler-generated stubs (asan.module_ctor and friends) carry no
        # checks on either side; they would only bloat the report.
        if (bc[f] + mc[f] == 0)
          continue
        printf "%s\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\n", file, f, suite,
               bc[f], mc[f], mc[f] - bc[f], bg[f], mg[f], mg[f] - bg[f]
      }
    }
  ' "$1" "$2"
}

compile_one() { # $1: source, $2: output, $3: 1 to enable MAIR, $4: suite
  local src=$1
  local out=$2
  local with_mair=$3
  local sample_suite=$4
  local cc=$CCXX
  if [[ $src == *.c ]]; then
    cc=$CCC
  fi
  local flags=(-"$sample_suite" -O1)
  # Keep native ASan's own optimization out of the picture on both sides, so
  # the counts measure instrumentation and not middle-end luck.
  if [[ $sample_suite == asan ]]; then
    flags+=(-mllvm -asan-opt=false)
  fi
  if [[ $with_mair == 1 ]]; then
    MAIR=1 "$cc" "${flags[@]}" -S -emit-llvm "$src" -o "$out"
  else
    env -u MAIR "$cc" "${flags[@]}" -S -emit-llvm "$src" -o "$out"
  fi
}

SAMPLE_SUITES=()
if [[ $SUITE == asan || $SUITE == all ]]; then
  SAMPLE_SUITES+=(asan)
fi
if [[ $SUITE == tsan || $SUITE == all ]]; then
  SAMPLE_SUITES+=(tsan)
fi

FILES=()
FILE_SUITES=()
for sample_suite in "${SAMPLE_SUITES[@]}"; do
  for f in "$REPO_DIR"/test/"$sample_suite"/TestCases/mair-*; do
    if [[ -f $f && ($f == *.c || $f == *.cpp) ]]; then
      FILES+=("$f")
      FILE_SUITES+=("$sample_suite")
    fi
  done
done
if [[ ${#FILES[@]} -eq 0 ]]; then
  echo "no mair-* samples found for suite '$SUITE'" >&2
  exit 2
fi

printf 'file\tfunc\tsanitizer\tcount_baseline\tcount_mair\tdelta\tguarded_baseline\tguarded_mair\tguarded_delta\n' > "$REPORT"

SAMPLES=0
for i in "${!FILES[@]}"; do
  src=${FILES[$i]}
  cur_suite=${FILE_SUITES[$i]}
  rel=${src#"$REPO_DIR/"}
  # test/tsan/TestCases/mair-ignore.c is an LLVM IR snippet with a -x ir RUN
  # line; it is not a C sample and cannot be compiled here.
  if head -n 1 "$src" | grep -q -- '-x ir'; then
    echo "skip $rel: LLVM IR sample, not C/C++" >&2
    continue
  fi
  if ! compile_one "$src" "$WORK/base.ll" 0 "$cur_suite" 2>"$WORK/err"; then
    echo "skip $rel: baseline compile failed" >&2
    sed -n '1,5p' "$WORK/err" >&2 || true
    continue
  fi
  if ! compile_one "$src" "$WORK/mair.ll" 1 "$cur_suite" 2>"$WORK/err"; then
    echo "skip $rel: MAIR compile failed" >&2
    sed -n '1,5p' "$WORK/err" >&2 || true
    continue
  fi
  count_checks "$WORK/base.ll" "$cur_suite" > "$WORK/base.rec"
  count_checks "$WORK/mair.ll" "$cur_suite" > "$WORK/mair.rec"
  merge_records "$WORK/base.rec" "$WORK/mair.rec" "$rel" "$cur_suite" >> "$REPORT"
  SAMPLES=$((SAMPLES + 1))
done

if [[ $SAMPLES -eq 0 ]]; then
  echo "every sample failed to compile; nothing to compare" >&2
  exit 1
fi

if [[ -n "$OUT" ]]; then
  mkdir -p "$(dirname -- "$OUT")"
  cp "$REPORT" "$OUT"
else
  cat "$REPORT"
fi

STATUS=0
if [[ $FAIL_ON_MISSING == 1 ]]; then
  if ! awk -F '\t' '
      FNR == 1 { next }
      $9 < 0 {
        printf "fewer guarded bytes under MAIR: %s %s %d -> %d (%+d)\n", $1, $2, $7, $8, $9 > "/dev/stderr"
        bad = 1
      }
      END { exit bad ? 1 : 0 }
    ' "$REPORT"; then
    STATUS=1
  fi
fi
if [[ $FAIL_ON_CALL_COUNT == 1 ]]; then
  if ! awk -F '\t' '
      FNR == 1 { next }
      $6 < 0 {
        printf "fewer checks under MAIR: %s %s %d -> %d (%+d)\n", $1, $2, $4, $5, $6 > "/dev/stderr"
        bad = 1
      }
      END { exit bad ? 1 : 0 }
    ' "$REPORT"; then
    STATUS=1
  fi
fi

ROWS=$(($(wc -l < "$REPORT") - 1))
echo "compared $SAMPLES samples ($ROWS function rows), gate $([[ $STATUS == 1 ]] && echo FAILED || echo OK)" >&2
exit "$STATUS"
