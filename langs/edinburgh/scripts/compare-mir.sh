#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
LANG_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd)
ROOT=$(cd -- "$LANG_DIR/../.." && pwd)

INPUT=${1:-$LANG_DIR/examples/tiny.ll}
PLUGIN="$ROOT/build/lib/libedinburgh_sched.so"
OUTDIR="$ROOT/build/edinburgh"
BASE=$(basename "$INPUT")
STEM=${BASE%.*}

mkdir -p "$OUTDIR"

DEFAULT_MIR="$OUTDIR/$STEM.default.mir"
EDINBURGH_MIR="$OUTDIR/$STEM.edinburgh.mir"

print_gap() {
  printf '\n\n\n\n\n'
}

print_section() {
  printf '===== %s =====\n\n' "$1"
}

llc-20 -mtriple=x86_64-pc-linux-gnu -O2 -enable-misched \
  -stop-after=machine-scheduler "$INPUT" -o "$DEFAULT_MIR"

llc-20 -mtriple=x86_64-pc-linux-gnu -O2 -load "$PLUGIN" \
  -enable-misched -misched=edinburgh -stop-after=machine-scheduler \
  "$INPUT" -o "$EDINBURGH_MIR"

print_gap
print_section "LLVM default scheduler MIR"
cat "$DEFAULT_MIR"

print_gap
print_section "Edinburgh scheduler MIR"
cat "$EDINBURGH_MIR"

print_gap
print_section "Diff"
diff -u "$DEFAULT_MIR" "$EDINBURGH_MIR" || true

print_gap
printf 'Default MIR:   %s\n' "$DEFAULT_MIR"
printf 'Edinburgh MIR: %s\n' "$EDINBURGH_MIR"
