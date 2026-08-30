#!/usr/bin/env bash
# Headless DTLS-1.3 ClientHello bench: no ethernet, no sudo, no tap.
# Runs the bench firmware (boot/main.c dummy-IO) in litex_sim to locate where
# wolfSSL_connect stalls. Usage:  bash scripts/run_bench.sh [timeout_s]
set -u
cd "$(dirname "$0")/.." || exit 1
ROOT="$PWD"
OUT="${BENCH_OUT:-$ROOT/evidence/bench_sim.log}"
TMO="${1:-240}"
mkdir -p "$(dirname "$OUT")"
FLAGS="--cpu-type=vexriscv --cpu-variant=standard --integrated-main-ram-size=0x06400000 --csr-json csr.json"
# shellcheck disable=SC1091
source litex-env/bin/activate || { echo "venv activate failed"; exit 1; }
: > "$OUT"
echo "== headless bench: timeout ${TMO}s, out=$OUT =="
timeout "$TMO" litex_sim $FLAGS --ram-init=boot/boot.bin --non-interactive > "$OUT" 2>&1
echo "== sim exited (code $?) =="
echo "----- tail -----"
tail -30 "$OUT"
