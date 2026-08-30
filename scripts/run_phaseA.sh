#!/usr/bin/env bash
# Phase A transport test: bring up tap0, start the UDP echo server + tcpdump,
# run the LiteX sim with the UDP-client firmware, and capture evidence.
# Run from the project root:  ! bash scripts/run_phaseA.sh
# Needs sudo (tap0 + tcpdump + the sim self-sudos obj_dir/Vsim).
set -u
cd "$(dirname "$0")/.." || exit 1
ROOT="$PWD"
EVID="$ROOT/evidence"; mkdir -p "$EVID"
FLAGS="--cpu-type=vexriscv --cpu-variant=standard --integrated-main-ram-size=0x06400000 --with-ethernet --csr-json csr.json"

# shellcheck disable=SC1091
source litex-env/bin/activate || { echo "venv activate failed"; exit 1; }

echo "== caching sudo credentials =="
sudo -v || { echo "sudo failed"; exit 1; }
# keep sudo alive during the run
( while true; do sudo -n true; sleep 30; done ) & KEEPALIVE=$!
trap 'kill $KEEPALIVE 2>/dev/null' EXIT

echo "== ensuring tap0 (192.168.1.100/24) =="
if ! ip link show tap0 >/dev/null 2>&1; then
  sudo ip tuntap add mode tap tap0 user "$USER"
fi
ip addr show tap0 | grep -q "192.168.1.100" || sudo ip addr add 192.168.1.100/24 dev tap0
sudo ip link set dev tap0 up
ip addr show tap0 | sed 's/^/   tap0: /'

echo "== starting UDP echo server (192.168.1.100:1234) =="
pkill -f "host/server" 2>/dev/null
"$ROOT/host/server" > "$EVID/phaseA_server.log" 2>&1 & SRV=$!
echo "   server pid $SRV"

echo "== starting tcpdump on tap0 =="
sudo pkill -f "tcpdump -i tap0" 2>/dev/null
sudo tcpdump -i tap0 -n -w "$EVID/phaseA_udp.pcap" 'udp or arp' > "$EVID/phaseA_tcpdump.log" 2>&1 &
sleep 1

echo "== launching sim (background); waiting for the handshake message =="
: > "$EVID/phaseA_sim.log"
( timeout 300 litex_sim $FLAGS --ram-init=boot/boot.bin --non-interactive > "$EVID/phaseA_sim.log" 2>&1 ) & SIMWRAP=$!

# Wait up to ~4 min for the server to receive the client's message, or the sim
# UART to show the server's greeting (either direction proves transport).
OK=0
for i in $(seq 1 240); do
  if grep -q "Hello from LiteX SoC" "$EVID/phaseA_server.log" 2>/dev/null; then OK=1; echo "   [+] server received client message (t=${i}s)"; fi
  if grep -q "Hi I am the linux server" "$EVID/phaseA_sim.log" 2>/dev/null; then OK=2; echo "   [+] sim received server greeting (t=${i}s)"; fi
  [ "$OK" = 2 ] && break
  if ! kill -0 $SIMWRAP 2>/dev/null; then echo "   [!] sim process exited early (t=${i}s)"; break; fi
  sleep 1
done

echo "== tearing down =="
sudo pkill -f "obj_dir/Vsim" 2>/dev/null
kill $SIMWRAP 2>/dev/null
sudo pkill -f "tcpdump -i tap0" 2>/dev/null
kill $SRV 2>/dev/null
sleep 1

echo;    echo "===================== SERVER LOG ====================="; cat "$EVID/phaseA_server.log" 2>/dev/null
echo;    echo "================== SIM UART LOG (tail) =============="; tail -50 "$EVID/phaseA_sim.log" 2>/dev/null
echo;    echo "===================== PCAP SUMMARY =================="; sudo tcpdump -r "$EVID/phaseA_udp.pcap" -n 2>/dev/null | head -40
echo;    echo "===================== RESULT ========================"
case "$OK" in
  2) echo "PASS: bidirectional UDP (SoC<->server) verified." ;;
  1) echo "PARTIAL: SoC->server worked; server->SoC greeting not seen in sim log." ;;
  *) echo "FAIL: no UDP exchange observed. Inspect logs above + evidence/." ;;
esac
echo "Evidence saved under: $EVID"
