#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUT="test-evidence/s9_${STAMP}"
mkdir -p "$OUT"

printf 'SimpleShell S9 Validation\nTimestamp: %s\n' "$STAMP" | tee "$OUT/summary.txt"
printf '\n[1/4] Clean build\n'
if make clean >"$OUT/clean.log" 2>&1 && make >"$OUT/build.log" 2>&1; then
  echo 'BUILD: PASS' | tee -a "$OUT/summary.txt"
else
  echo 'BUILD: FAIL' | tee -a "$OUT/summary.txt"
  cat "$OUT/build.log" "$OUT/clean.log"
  exit 1
fi

printf '\n[2/4] Full regression and interactive test suite\n'
if make test >"$OUT/regression_tests.log" 2>&1; then
  echo 'REGRESSION TESTS: PASS' | tee -a "$OUT/summary.txt"
else
  echo 'REGRESSION TESTS: FAIL' | tee -a "$OUT/summary.txt"
  cat "$OUT/regression_tests.log"
  exit 1
fi

printf '\n[3/4] Valgrind memory check\n'
if command -v valgrind >/dev/null 2>&1; then
  if make memcheck >"$OUT/memcheck.log" 2>&1; then
    echo 'VALGRIND: PASS (see memcheck.log for allocation summary)' | tee -a "$OUT/summary.txt"
  else
    echo 'VALGRIND: FAIL' | tee -a "$OUT/summary.txt"
    cat "$OUT/memcheck.log"
    exit 1
  fi
else
  echo 'VALGRIND: SKIPPED (not installed)' | tee -a "$OUT/summary.txt"
fi

printf '\n[4/4] Validation summary\n'
echo "Evidence directory: $OUT" | tee -a "$OUT/summary.txt"
echo 'OVERALL: PASS for checks that ran; review any SKIPPED checks.' | tee -a "$OUT/summary.txt"
echo "\nLogs saved in: $OUT"
