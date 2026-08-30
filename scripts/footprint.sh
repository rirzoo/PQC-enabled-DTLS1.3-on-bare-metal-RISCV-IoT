#!/usr/bin/env bash
#
# footprint.sh — record the client firmware's memory footprint and DTLS handshake
# latency, keyed to what the client can currently do, so future optimizations can
# be compared against a baseline.
#
#   Footprint comes from the ELF (no sim run needed):
#     .text/.rodata/.data/.bss via `<triple>-size -A`, loadable image = boot.bin size.
#     The Berkeley `size` bss is ignored on purpose — it folds in the fixed 16 MiB
#     ._user_heap static pool and overstates RAM by ~16 MB.
#
#   Handshake latency is scraped from a sim UART log line emitted by the firmware:
#     "  handshake time: <N> ms ..."  (see boot/main.c). Deterministic sim-time,
#     independent of how fast Verilator runs on the host.
#
# Usage:
#   scripts/footprint.sh [-c "capability label"] [-l sim.log] [-e boot.elf] [-a]
#     -c  short label of what the client can do (default: read from git/branch)
#     -l  sim UART log to scrape handshake time from (default: newest evidence/phaseB_sim.log)
#     -e  path to the firmware ELF (default: boot/boot.elf)
#     -a  append a row to the CSV ledger evidence/footprint.csv (in addition to printing)
#
# Prints a Markdown table row (paste into PROGRESS.md) and a human summary.

set -euo pipefail

# --- locate repo root (script lives in scripts/) ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT"

CAP=""
SIM_LOG=""
ELF="boot/boot.elf"
APPEND=0
while getopts "c:l:e:ah" opt; do
    case "$opt" in
        c) CAP="$OPTARG" ;;
        l) SIM_LOG="$OPTARG" ;;
        e) ELF="$OPTARG" ;;
        a) APPEND=1 ;;
        h) sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "bad option; use -h" >&2; exit 2 ;;
    esac
done

BIN="${ELF%.elf}.bin"

# --- pick a size(1) that matches the toolchain ---
SIZE=""
for cand in riscv64-linux-gnu-size riscv64-unknown-elf-size riscv-none-elf-size size; do
    if command -v "$cand" >/dev/null 2>&1; then SIZE="$cand"; break; fi
done
[ -n "$SIZE" ] || { echo "no *-size tool found on PATH" >&2; exit 1; }
[ -f "$ELF" ]  || { echo "ELF not found: $ELF (build boot/ first)" >&2; exit 1; }

# --- section sizes from SysV layout (-A) ---
sect() { "$SIZE" -A "$ELF" | awk -v s="$1" '$1==s {print $2; found=1} END{if(!found)print 0}'; }
TEXT=$(sect .text)
RODATA=$(sect .rodata)
DATA=$(sect .data)
BSS=$(sect .bss)

# --- loadable image size ---
if [ -f "$BIN" ]; then
    LOAD=$(stat -c%s "$BIN")
else
    LOAD="?"
fi

# --- handshake latency: scrape newest matching sim log unless one was given ---
if [ -z "$SIM_LOG" ]; then
    SIM_LOG=$(ls -t evidence/phaseB_sim.log 2>/dev/null | head -1 || true)
fi
HS_MS="—"
if [ -n "${SIM_LOG:-}" ] && [ -f "$SIM_LOG" ]; then
    m=$(grep -oE 'handshake time:[[:space:]]*[0-9]+[[:space:]]*ms' "$SIM_LOG" | tail -1 \
        | grep -oE '[0-9]+' | head -1 || true)
    [ -n "${m:-}" ] && HS_MS="$m"
fi

# --- default capability label ---
if [ -z "$CAP" ]; then
    CAP="(describe client capability set)"
fi

DATE=$(date +%Y-%m-%d)
SHA=$(git rev-parse --short HEAD 2>/dev/null || echo "nogit")

# --- human summary ---
kib() { awk -v b="$1" 'BEGIN{printf "%.1f", b/1024}'; }
echo "firmware footprint  (ELF: $ELF, size tool: $SIZE)"
echo "  .text   : $TEXT B ($(kib "$TEXT") KiB)   [code]"
echo "  .rodata : $RODATA B ($(kib "$RODATA") KiB)   [const data -> ROM]"
echo "  .data   : $DATA B"
echo "  .bss    : $BSS B ($(kib "$BSS") KiB)   [static RAM; excludes 16 MiB _user_heap]"
echo "  loadable: $LOAD B ($( [ "$LOAD" != "?" ] && kib "$LOAD" || echo "?") KiB)   [boot.bin]"
echo "  handshake: $HS_MS ms   (from ${SIM_LOG:-<none>})"
echo "  commit  : $SHA"
echo

# --- Markdown row for PROGRESS.md ---
echo "Markdown row (paste into the footprint table):"
echo "| $DATE | $CAP | $TEXT | $RODATA | $DATA | $BSS | $LOAD B | $HS_MS ms |"

# --- optional CSV ledger ---
if [ "$APPEND" -eq 1 ]; then
    CSV="evidence/footprint.csv"
    if [ ! -f "$CSV" ]; then
        echo "date,commit,capability,text,rodata,data,bss,loadable_bin,handshake_ms" > "$CSV"
    fi
    echo "$DATE,$SHA,\"$CAP\",$TEXT,$RODATA,$DATA,$BSS,$LOAD,$HS_MS" >> "$CSV"
    echo
    echo "appended to $CSV"
fi
