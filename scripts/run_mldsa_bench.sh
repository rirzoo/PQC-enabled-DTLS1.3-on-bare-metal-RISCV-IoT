#!/usr/bin/env bash
# ML-DSA-44 transport benchmark: a controlled 2x2 matrix isolating the two RX
# levers, with ML-DSA-44 (Dilithium2) server auth held constant in every cell.
#
#   Cell | RX method                  | nrxslots | DTLS_PACE_US
#   -----+----------------------------+----------+-------------
#   A1   | poll  (server pacing)      |    2     |   25000
#   A2   | poll  (server pacing)      |    4     |   25000
#   B1   | interrupt (-DDTLS_RX_IRQ)  |    2     |       0
#   B2   | interrupt (-DDTLS_RX_IRQ)  |    4     |       0
#
# Per nrxslots N: set nrxslots in litex_sim.py -> regenerate build/sim headers ->
# COMPILE obj_dir/Vsim once (the Verilator model bakes in the RX-slot count, so it
# must be rebuilt per N). Then, per cell: `make clean && make [DTLS_RX_IRQ=1]` (the
# firmware also bakes ETHMAC_RX_SLOTS via udp.c's TX-buffer offset) -> refresh
# sim_main_ram.init from the fresh boot.bin -> launch the server with the cell's
# pacing -> run the PREBUILT obj_dir/Vsim directly (Vsim $readmemh's the .init at
# start, so only the firmware image changes between the two cells of an N) -> scrape
# handshake latency + firmware footprint -> append a row to evidence/mldsa_bench.csv.
#
# Why build the model once per N (not per cell): litex_sim defers the Verilator
# compile with an opaque staleness check that, after regen_headers kills prep before
# verilate, left obj_dir/Vsim STALE (built for the previous N) for the FIRST cell of
# each N -> the model's RX-slot count != the firmware's baked ETHMAC_RX_SLOTS -> TX
# landed in the wrong SRAM region -> zero packets -> ARP hang. Only the *second* cell
# got the rebuilt model. Building the model explicitly (build_model) and running
# obj_dir/Vsim directly removes that race entirely; the failure was positional
# (first-of-N), NOT poll-vs-irq -- proven by reversing the order and watching the
# failure follow the first slot regardless of RX method.
#
# Run from the project root:  ! bash scripts/run_mldsa_bench.sh
# Needs sudo (tap0 + tcpdump + the sim self-sudos obj_dir/Vsim).
set -u
cd "$(dirname "$0")/.." || exit 1
ROOT="$PWD"
EVID="$ROOT/evidence"; mkdir -p "$EVID"
WOLF="$ROOT/host/wolfssl"
BOOT="$ROOT/boot"
SIMPY="$ROOT/litex/litex/tools/litex_sim.py"
SOC_H="$ROOT/build/sim/software/include/generated/soc.h"
CSV="$EVID/mldsa_bench.csv"
FLAGS="--cpu-type=vexriscv --cpu-variant=standard --integrated-main-ram-size=0x06400000 --with-ethernet --csr-json csr.json"
NPROC="$(nproc)"

# shellcheck disable=SC1091
source litex-env/bin/activate || { echo "venv activate failed"; exit 1; }
export LD_LIBRARY_PATH="$WOLF/src/.libs:${LD_LIBRARY_PATH:-}"

# ---- restore litex_sim.py on any exit (it is a vendored file we edit in place) ----
SIMPY_BAK="$(mktemp)"
cp "$SIMPY" "$SIMPY_BAK"
KEEPALIVE=""
cleanup() {
  echo "== restoring litex_sim.py + tearing down =="
  cp "$SIMPY_BAK" "$SIMPY"; rm -f "$SIMPY_BAK"
  [ -n "$KEEPALIVE" ] && kill "$KEEPALIVE" 2>/dev/null
  sudo pkill -f "obj_dir/Vsim" 2>/dev/null
  sudo pkill -f "tcpdump -i tap0" 2>/dev/null
  pkill -f "server_dtls" 2>/dev/null
}
trap cleanup EXIT

echo "== caching sudo credentials =="
sudo -v || { echo "sudo failed"; exit 1; }
( while true; do sudo -n true; sleep 30; done ) & KEEPALIVE=$!

echo "== ensuring tap0 (192.168.1.100/24) =="
if ! ip link show tap0 >/dev/null 2>&1; then sudo ip tuntap add mode tap tap0 user "$USER"; fi
ip addr show tap0 | grep -q "192.168.1.100" || sudo ip addr add 192.168.1.100/24 dev tap0
sudo ip link set dev tap0 up

echo "== building host DTLS server (loads the ML-DSA-44 cert/key) =="
( cd host && gcc server_dtls.c -I wolfssl -L wolfssl/src/.libs -lwolfssl \
    -Wl,-rpath,"$WOLF/src/.libs" -o server_dtls ) || { echo "server build failed"; exit 1; }

# Fresh CSV per full-matrix run (a stale one from an earlier run would append duplicate
# rows). Back up any existing CSV first so no prior evidence is silently lost.
if [ -f "$CSV" ]; then cp "$CSV" "$CSV.$(date +%Y%m%d_%H%M%S).bak"; fi
echo "cell,rx_method,nrxslots,pace_us,handshake_ms,text,rodata,data,bss,loadable_bin,rx_sram_bytes,result" > "$CSV"

# set_nrxslots N : restore pristine litex_sim.py, inject `nrxslots = N,` into the
# single LiteEthMAC(...) call (anchored on the unique dw= line).
set_nrxslots() {
  local n="$1"
  cp "$SIMPY_BAK" "$SIMPY"
  sed -i "/dw *= *64 if ethernet_phy_model == \"xgmii\" else 32,/a\\                nrxslots   = ${n}," "$SIMPY"
}

# regen_headers N : run litex_sim just long enough to regenerate build/sim headers
# (they are written before the slow Verilator compile), then kill it. No sudo /
# no Vsim launch happens this early, so nothing privileged is left behind.
regen_headers() {
  local n="$1" i
  echo "   [prep] regenerating build/sim headers for nrxslots=$n ..."
  # Run under setsid so litex_sim is its OWN process-group leader. A plain `( ... ) &`
  # in a script (job control off) shares the shell's process group, so `kill $pid` would
  # reap only the subshell and ORPHAN the litex_sim -> make -> verilator/g++ tree, which
  # then keeps running and races the first run_cell's gateware rebuild / grabs tap0 with a
  # second Vsim. That silently broke the first cell of each nrxslots. setsid lets us kill
  # the whole group cleanly (and safely -- it is not the script's group).
  setsid litex_sim $FLAGS --ram-init=boot/boot.bin --non-interactive \
      > "$EVID/mldsa_prep_n${n}.log" 2>&1 & local pid=$!
  for i in $(seq 1 180); do
    if grep -qE "define ETHMAC_RX_SLOTS $n$" "$SOC_H" 2>/dev/null; then
      echo "   [prep] headers show ETHMAC_RX_SLOTS=$n (t=${i}s)"; break
    fi
    kill -0 "$pid" 2>/dev/null || { echo "   [prep] litex_sim exited early; see mldsa_prep_n${n}.log"; break; }
    sleep 1
  done
  # Kill the ENTIRE prep process group (litex_sim + make + verilator + g++), then reap any
  # stray sim process, and let the filesystem settle before run_cell rebuilds the gateware.
  local pgid selfpgid
  pgid=$(ps -o pgid= -p "$pid" 2>/dev/null | tr -d ' ')
  selfpgid=$(ps -o pgid= -p $$ 2>/dev/null | tr -d ' ')
  # Only group-kill if setsid really put litex_sim in its OWN group (never the script's).
  if [ -n "$pgid" ] && [ "$pgid" != "$selfpgid" ]; then kill -TERM -"$pgid" 2>/dev/null; fi
  kill -TERM "$pid" 2>/dev/null; wait "$pid" 2>/dev/null
  pkill -TERM -f "tools/litex_sim" 2>/dev/null
  sudo pkill -f "obj_dir/Vsim" 2>/dev/null
  sleep 3
  grep -qE "define ETHMAC_RX_SLOTS $n$" "$SOC_H" || {
    echo "   [prep] FAILED to regenerate headers for nrxslots=$n"; return 1; }
}

# build_model N : compile obj_dir/Vsim to completion for the current nrxslots. regen_headers
# already (re)generated sim.v / build_sim.sh / sim_config.js for N; this forces the Verilator
# O3 compile that litex_sim otherwise defers with an opaque staleness check (which silently ran
# a STALE model for the first cell of each N). Building it here, once per N, and then running
# obj_dir/Vsim directly per cell removes that race. build_sim.sh already does `rm -rf obj_dir/`.
build_model() {
  local n="$1" gw="$ROOT/build/sim/gateware" vsim
  vsim="$gw/obj_dir/Vsim"
  echo "   [model] compiling obj_dir/Vsim for nrxslots=$n (Verilator O3; one-time per N) ..."
  ( cd "$gw" && bash build_sim.sh ) > "$EVID/mldsa_model_n${n}.log" 2>&1 \
    || { echo "   [model] build FAILED -- see mldsa_model_n${n}.log"; return 1; }
  if [ ! -x "$vsim" ]; then echo "   [model] no obj_dir/Vsim produced"; return 1; fi
  # The compiled model must be newer than the sim.v it was generated from, else it is stale.
  if [ "$vsim" -ot "$gw/sim.v" ]; then
    echo "   [model] obj_dir/Vsim older than sim.v -- stale model, refusing"; return 1; fi
  echo "   [model] Vsim built for nrxslots=$n"
}

# run_cell CELL METHOD N PACE
run_cell() {
  local cell="$1" method="$2" n="$3" pace="$4"
  local irqflag="" tag="$cell"
  [ "$method" = "irq" ] && irqflag="DTLS_RX_IRQ=1"
  local simlog="$EVID/mldsa_${cell}_sim.log"
  local srvlog="$EVID/mldsa_${cell}_server.log"
  local pcap="$EVID/mldsa_${cell}.pcap"

  echo
  echo "############################################################"
  echo "# Cell $cell : method=$method nrxslots=$n DTLS_PACE_US=$pace"
  echo "############################################################"

  echo "   [build] make clean && make $irqflag"
  ( cd "$BOOT" && make clean >/dev/null 2>&1 && make $irqflag -j"$NPROC" ) \
      > "$EVID/mldsa_${cell}_build.log" 2>&1 || { echo "   [build] FAILED"; return 1; }

  # Footprint of the just-built firmware.
  local text rodata data bss loadable rxsram
  text=$(riscv64-linux-gnu-size -A "$BOOT/boot.elf"   | awk '$1==".text"{print $2}')
  rodata=$(riscv64-linux-gnu-size -A "$BOOT/boot.elf" | awk '$1==".rodata"{print $2}')
  data=$(riscv64-linux-gnu-size -A "$BOOT/boot.elf"   | awk '$1==".data"{print $2}')
  bss=$(riscv64-linux-gnu-size -A "$BOOT/boot.elf"    | awk '$1==".bss"{print $2}')
  loadable=$(stat -c%s "$BOOT/boot.bin")
  rxsram=$(( n * 2048 ))

  # --- Refresh the RAM-init from THIS cell's freshly built firmware. litex_sim only
  #     writes sim_main_ram.init while it *builds* the gateware; a launch that reuses an
  #     up-to-date build keeps the PREVIOUS firmware image. That silently ran n=2 firmware
  #     on the n=4 gateware before -> udp.c's baked ETHMAC_RX_SLOTS put TX in the RX region
  #     -> dead TX / ARP hang. Regenerate it explicitly (byte-identical to litex's own). ---
  local raminit="$ROOT/build/sim/gateware/sim_main_ram.init"
  echo "   [raminit] regenerating from boot.bin (little-endian)"
  python "$ROOT/scripts/gen_raminit.py" "$BOOT/boot.bin" "$raminit" little \
      > "$EVID/mldsa_${cell}_raminit.log" 2>&1 || { echo "   [raminit] FAILED"; return 1; }

  # Guard: init must be no older than boot.bin, and the gateware headers must match this
  # cell's slot count (else firmware/gateware TX offsets disagree and TX is silently dead).
  if [ "$raminit" -ot "$BOOT/boot.bin" ]; then
      echo "   [guard] RAM-init older than boot.bin -- refusing to launch $cell"; return 1; fi
  local soc_slots
  soc_slots=$(grep -oE "define ETHMAC_RX_SLOTS [0-9]+" "$SOC_H" | grep -oE "[0-9]+$")
  if [ "$soc_slots" != "$n" ]; then
      echo "   [guard] soc.h ETHMAC_RX_SLOTS=$soc_slots != cell n=$n -- refusing to launch $cell"; return 1; fi
  # Guard: the compiled model must exist and be newer than sim.v (build_model built it for this
  # N). This is the check whose absence let the first-of-N cell run a stale model built for the
  # previous N -> dead TX -> ARP hang.
  local vsim="$ROOT/build/sim/gateware/obj_dir/Vsim"
  if [ ! -x "$vsim" ] || [ "$vsim" -ot "$ROOT/build/sim/gateware/sim.v" ]; then
      echo "   [guard] obj_dir/Vsim missing or stale vs sim.v -- refusing to launch $cell"; return 1; fi

  # Clean slate: ensure no stray sim (from prep or a prior cell) is still holding tap0 or
  # racing this cell's gateware rebuild before we bring the transport up.
  sudo pkill -f "obj_dir/Vsim" 2>/dev/null
  pkill -TERM -f "tools/litex_sim" 2>/dev/null
  sleep 1

  echo "   [server] starting (DTLS_PACE_US=$pace)"
  pkill -f "server_dtls" 2>/dev/null; sleep 1
  ( cd host && DTLS_PACE_US="$pace" ./server_dtls 192.168.1.100 ) > "$srvlog" 2>&1 & local SRV=$!

  echo "   [tcpdump] $pcap"
  sudo pkill -f "tcpdump -i tap0" 2>/dev/null
  sudo tcpdump -i tap0 -n -w "$pcap" 'udp or arp' > "$EVID/mldsa_${cell}_tcpdump.log" 2>&1 &
  sleep 1

  echo "   [sim] launching prebuilt obj_dir/Vsim directly; waiting for handshake ..."
  : > "$simlog"
  # Run the already-compiled model (run_sim.sh is literally `sudo obj_dir/Vsim`). Vsim reads
  # sim_config.js (tap0, IPs) + sim_main_ram.init by fixed name from the gateware dir; no build,
  # no litex_sim staleness heuristic. `sudo timeout` so the backstop kill runs as root and
  # actually reaches Vsim; </dev/null so the boot-abort prompt just times out and proceeds.
  ( cd "$ROOT/build/sim/gateware" && sudo timeout 1200 obj_dir/Vsim < /dev/null > "$simlog" 2>&1 ) & local SIMWRAP=$!

  local OK=0 i arp_since=0
  for i in $(seq 1 1000); do
    grep -q "HANDSHAKE COMPLETE" "$simlog" 2>/dev/null && OK=1
    grep -q "server says"        "$simlog" 2>/dev/null && OK=2
    if grep -qiE "^FAIL|handshake timed out" "$simlog" 2>/dev/null; then echo "   [sim] failure reported (t=${i}s)"; break; fi
    [ "$OK" = 2 ] && { echo "   [sim] app-data round trip (t=${i}s)"; break; }
    # Fail-fast on a TX/gateware mismatch: the firmware's ARP loop has no timeout, so it
    # would hang forever. Start a 40 s window only once the firmware has actually reached
    # its ARP loop (so a slow first-of-N Verilator compile does not trip a false abort).
    if grep -q "Resolving server" "$simlog" 2>/dev/null; then
      [ "$arp_since" = 0 ] && arp_since=$i
      if ! grep -q "Server MAC resolved" "$simlog" 2>/dev/null && [ $((i - arp_since)) -ge 40 ]; then
        echo "   [sim] ARP unresolved 40s after boot -- aborting cell (TX/gateware mismatch?)"; break; fi
    fi
    kill -0 $SIMWRAP 2>/dev/null || { echo "   [sim] exited (t=${i}s)"; break; }
    sleep 1
  done

  # Scrape the handshake latency printed by the firmware.
  local hs
  hs=$(grep -oE "handshake time: [0-9]+ ms" "$simlog" 2>/dev/null | grep -oE "[0-9]+" | head -1)
  [ -z "$hs" ] && hs="NA"

  local result
  case "$OK" in
    2) result="PASS" ;;
    1) result="PARTIAL" ;;
    *) result="FAIL" ;;
  esac

  echo "   [teardown]"
  sudo pkill -f "obj_dir/Vsim" 2>/dev/null
  kill $SIMWRAP 2>/dev/null
  pkill -TERM -f "tools/litex_sim" 2>/dev/null   # reap litex_sim + any children if killed mid-build
  sudo pkill -f "tcpdump -i tap0" 2>/dev/null
  kill $SRV 2>/dev/null
  sleep 2

  echo "$cell,$method,$n,$pace,$hs,$text,$rodata,$data,$bss,$loadable,$rxsram,$result" >> "$CSV"
  echo "   [result] $cell: $result  handshake=${hs} ms  .text=$text  boot.bin=$loadable  rx_sram=${rxsram}B"
}

# ---- run the matrix, grouped by nrxslots: per N we regenerate headers, compile the
#      Verilator model ONCE (build_model), then run both cells against that prebuilt
#      model (poll/irq within an N share the identical gateware -- only firmware differs).
#      Cell order within an N is now irrelevant to correctness (the earlier first-of-N
#      ARP failure was a stale-model artifact, now fixed); we use the canonical A-then-B
#      order. Cell labels stay tied to their config (A=poll, B=irq).
# N=2
set_nrxslots 2 && regen_headers 2 && build_model 2 && {
  run_cell A1 poll 2 25000
  run_cell B1 irq  2 0
}
# N=4
set_nrxslots 4 && regen_headers 4 && build_model 4 && {
  run_cell A2 poll 4 25000
  run_cell B2 irq  4 0
}

echo
echo "===================== BENCHMARK MATRIX ====================="
column -t -s, "$CSV" 2>/dev/null || cat "$CSV"
echo
echo "Raw rows: $CSV"
echo "Per-cell evidence: $EVID/mldsa_<cell>_{sim,server,build}.log, mldsa_<cell>.pcap"
