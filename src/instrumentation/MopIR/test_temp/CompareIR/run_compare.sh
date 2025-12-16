#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="${SCRIPT_DIR}/build"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo "Running CMake..."
cmake ..
echo "Building..."
make -j"$(nproc)"

echo "\nRunning comparisons (ASan semantics)...\n"
STATUS=0
for ll in test_simple.ll test_redundant.ll test_redundant_complex.ll test_recurrence.ll; do
  if [[ -f "$ll" ]]; then
    echo "==> $ll"
    if ! ./CompareIR "$ll"; then STATUS=1; fi
    echo "-----------------------------"
  else
    echo "WARN: Missing $ll"
  fi
done

echo "\nRunning comparisons (TSan semantics)...\n"
for ll in test_simple.ll test_redundant.ll test_redundant_complex.ll test_recurrence.ll; do
  if [[ -f "$ll" ]]; then
    echo "==> $ll (TSan)"
    if ! ./CompareIR "$ll" --tsan; then STATUS=1; fi
    echo "-----------------------------"
  else
    echo "WARN: Missing $ll"
  fi
done

exit $STATUS
