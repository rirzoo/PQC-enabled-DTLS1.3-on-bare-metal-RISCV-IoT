#!/usr/bin/env bash
# Phase B test: PQC-DTLS 1.3 handshake between the RISC-V sim (client) and the
# host wolfSSL DTLS 1.3 server, with ML-KEM-512 + ECC cert. Captures evidence.
# Run from the project root:  ! bash scripts/run_phaseB.sh
# Needs sudo (tap0 + tcpdump + the sim self-sudos obj_dir/Vsim).
set -u
cd "$(dirname "$0")/.." || exit 1
ROOT="$PWD"
EVID="$ROOT/evidence"; mkdir -p "$EVID"
WOLF="$ROOT/host/wolfssl"
FLAGS="--cpu-type=vexriscv --cpu-variant=standard --integrated-main-ram-size=0x06400000 --with-ethernet --csr-json csr.json"

# shellcheck disable=SC1091
source litex-env/bin/activate || { echo "venv activate failed"; exit 1; }
export LD_LIBRARY_PATH="$WOLF/src/.libs:${LD_LIBRARY_PATH:-}"

echo "== caching sudo credentials =="
sudo -v || { echo "sudo failed"; exit 1; }
( while true; do sudo -n true; sleep 30; done ) & KEEPALIVE=$!
trap 'kill $KEEPALIVE 2>/dev/null' EXIT

echo "== ensuring tap0 (192.168.1.100/24) =="
if ! ip link show tap0 >/dev/null 2>&1; then sudo ip tuntap add mode tap tap0 user "$USER"; fi
ip addr show tap0 | grep -q "192.168.1.100" || sudo ip addr add 192.168.1.100/24 dev tap0
sudo ip link set dev tap0 up

echo "== building host DTLS server =="
( cd host && gcc server_dtls.c -I wolfssl -L wolfssl/src/.libs -lwolfssl \
    -Wl,-rpath,"$WOLF/src/.libs" -o server_dtls ) || { echo "server build failed"; exit 1; }

echo "== starting PQC-DTLS 1.3 server (192.168.1.100:11111) =="
pkill -f "server_dtls" 2>/dev/null
( cd host && ./server_dtls 192.168.1.100 ) > "$EVID/phaseB_server.log" 2>&1 & SRV=$!

echo "== starting tcpdump on tap0 =="
sudo pkill -f "tcpdump -i tap0" 2>/dev/null
sudo tcpdump -i tap0 -n -w "$EVID/phaseB_dtls.pcap" 'udp or arp' > "$EVID/phaseB_tcpdump.log" 2>&1 &
sleep 1

echo "== launching sim (background); waiting for handshake =="
: > "$EVID/phaseB_sim.log"
( timeout 1200 litex_sim $FLAGS --ram-init=boot/boot.bin --non-interactive > "$EVID/phaseB_sim.log" 2>&1 ) & SIMWRAP=$!

OK=0
for i in $(seq 1 900); do
  if grep -q "HANDSHAKE COMPLETE" "$EVID/phaseB_sim.log" 2>/dev/null; then OK=1; echo "   [+] sim: handshake complete (t=${i}s)"; fi
  if grep -q "server says" "$EVID/phaseB_sim.log" 2>/dev/null; then OK=2; echo "   [+] sim: app-data round trip (t=${i}s)"; fi
  if grep -qiE "^FAIL|error [0-9]" "$EVID/phaseB_sim.log" 2>/dev/null; then echo "   [!] sim reported failure (t=${i}s)"; break; fi
  [ "$OK" = 2 ] && break
  if ! kill -0 $SIMWRAP 2>/dev/null; then echo "   [!] sim exited (t=${i}s)"; break; fi
  sleep 1
done

echo "== tearing down =="
sudo pkill -f "obj_dir/Vsim" 2>/dev/null
kill $SIMWRAP 2>/dev/null
sudo pkill -f "tcpdump -i tap0" 2>/dev/null
kill $SRV 2>/dev/null
sleep 1

echo;  echo "===================== SERVER LOG ====================="; cat "$EVID/phaseB_server.log" 2>/dev/null
echo;  echo "================== SIM UART LOG (tail) =============="; tail -60 "$EVID/phaseB_sim.log" 2>/dev/null
echo;  echo "===================== PCAP SUMMARY =================="; sudo tcpdump -r "$EVID/phaseB_dtls.pcap" -n 2>/dev/null | head -60
echo;  echo "===================== RESULT ========================"
case "$OK" in
  2) echo "PASS: PQC-DTLS 1.3 handshake + app-data round trip." ;;
  1) echo "PARTIAL: handshake completed; app-data echo not confirmed in sim log." ;;
  *) echo "FAIL: handshake not completed. Inspect logs + evidence/." ;;
esac
echo "Evidence: $EVID/phaseB_*"
