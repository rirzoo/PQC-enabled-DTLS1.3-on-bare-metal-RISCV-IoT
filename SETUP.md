# Setup & Reproduction Guide

Everything needed to go from a fresh clone to a running PQC-DTLS 1.3 handshake between the
bare-metal RISC-V firmware (simulated) and the Linux host server. Written for **Fedora**
(where this was built and verified); Debian/Ubuntu package names are noted alongside where
they differ.

## What you'll end up with

- A LiteX simulated SoC (VexRiscv `standard`, rv32im) built with `litex_sim` (Verilator).
- Firmware (`boot/boot.bin`) linking wolfSSL/wolfCrypt: a DTLS 1.3 client doing an **ML-KEM-512**
  key exchange and verifying a post-quantum **ML-DSA-44 (Dilithium2)** server certificate
  (verify-only). RX is interrupt-driven by default (an ethmac ISR drains the MAC during crypto —
  the production config selected by the transport benchmark).
- A Linux-side DTLS 1.3 server (`host/server_dtls`) built against a locally-compiled wolfSSL.
- A `tap0` bridge carrying UDP/DTLS between the two over real Linux networking.

## 1. System prerequisites

```
# Fedora
sudo dnf install python3 python3-pip python3-virtualenv \
    verilator libevent-devel json-c-devel \
    gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu \
    autoconf automake libtool pkgconf-pkg-config \
    openssl tcpdump iproute2 git

# Debian / Ubuntu equivalent
sudo apt install python3 python3-pip python3-venv \
    verilator libevent-dev libjson-c-dev \
    gcc-riscv64-linux-gnu libc6-dev-riscv64-cross \
    autoconf automake libtool pkg-config \
    openssl tcpdump iproute2 git
```

**Toolchain note:** this project links wolfSSL against a **Linux-targeted** RISC-V cross-gcc
(`riscv64-linux-gnu-gcc`, freestanding mode: `-ffreestanding` etc. supplied by LiteX's
`common.mak`) — **not** the `riscv64-unknown-elf-gcc` toolchain that `litex_setup.py --gcc=riscv`
installs. Skip that flag (step 3 below) and install the distro package instead; a bare-metal
`unknown-elf` toolchain won't have the headers wolfSSL/wolfCrypt's build expects here.

Verified against: GCC 15.2.1 (riscv64-linux-gnu), Verilator 5.046, Python 3.13.

## 2. Clone the repository

```
git clone https://github.com/rirzoo/PQC-enabled-DTLS1.3-on-bare-metal-RISCV-IoT.git
cd PQC-enabled-DTLS1.3-on-bare-metal-RISCV-IoT
```

**Path warning:** LiteX bakes the **absolute path** of this directory into `build/sim`'s
generated Makefiles and into the Python venv's editable-install finders + `bin/*` shebangs,
the moment you run setup below. Clone somewhere **without spaces in the path**, and don't
move/rename the directory afterward — doing so breaks the venv (`ModuleNotFoundError: litex`)
and the firmware Makefile (`SOC_DIRECTORY` gone).

## 3. Python venv + LiteX ecosystem

```
python3 -m venv litex-env
source litex-env/bin/activate
chmod +x litex_setup.py
./litex_setup.py --init --install     # clones litex/liteeth/litedram/... and pip-installs them editable
pip3 install meson ninja
```

Do **not** pass `--gcc=riscv` (see the toolchain note above).

## 4. Generate the simulated SoC (with Ethernet)

All `litex_sim` invocations from here on **must use the same flag set** — a mismatch between
the run that generates `build/sim`'s headers and the run that loads firmware produces a CSR
map the firmware doesn't match (silent hardware-not-found bugs).

```
FLAGS="--cpu-type=vexriscv --cpu-variant=standard --integrated-main-ram-size=0x06400000 --with-ethernet --csr-json csr.json"

litex-env/bin/litex_sim $FLAGS --non-interactive
# Kill it (Ctrl+C) once you see the software libs finish building — you don't need the sim
# to actually run yet, just the header/library generation under build/sim/.
```

Sanity check: `grep -c ethmac build/sim/software/include/generated/csr.h` should print a
number > 0.

## 5. Build the firmware

```
cd boot && make clean && make && cd ..
```

Produces `boot/boot.elf` and `boot/boot.bin`. This links the vendored `boot/wolfssl` +
`boot/wolfcrypt` sources (tracked in this repo — no separate clone needed for the firmware
side) with `-DWOLFSSL_USER_SETTINGS -mstrict-align` (see `boot/wolfssl/wolfcrypt/
user_settings.h`, `boot/Makefile`). The `-mstrict-align` flag is **required**: VexRiscv
`standard` has no hardware unaligned load/store, and an unaligned access hangs the CPU in an
infinite trap loop instead of faulting cleanly.

## 6. Build wolfSSL on the host

The host server links against a **separately built** wolfSSL clone (not vendored — this is a
full upstream checkout with its own `.git`, kept out of this repo).

```
git clone https://github.com/wolfSSL/wolfssl.git host/wolfssl
cd host/wolfssl
git checkout v5.8.4-stable   # or the current release; verified against 5.8.4
./autogen.sh
./configure --enable-tls13 --enable-dtls --enable-dtls13 --enable-dtls-frag-ch \
    --enable-mlkem --enable-dilithium --enable-certgen --enable-keygen \
    --enable-experimental --enable-curve25519 --enable-ed25519
make -j"$(nproc)"
cd ../..
```

`--enable-dtls-frag-ch` is **required** for DTLS 1.3 + PQC — the ClientHello carrying the
ML-KEM key share is too large for one datagram and must fragment. `--enable-dtls13` alone is
rejected by `configure`; it needs `--enable-dtls --enable-tls13` alongside it.
`--enable-dilithium --enable-certgen --enable-keygen` are needed for the **ML-DSA-44** server
authentication: the host mints and serves an ML-DSA-signed cert, and `scripts/gen_certs.sh`
(step 7) links this build to generate it (the system OpenSSL cannot mint ML-DSA certs).

## 7. Generate the server certificate (ML-DSA-44)

The repo tracks the server's public cert (`host/certs/server-cert.{pem,der}`) and the
firmware's embedded copy of it (`boot/certs.h`), but **not** the private key — so you must
regenerate a matching ML-DSA-44 key + cert pair. The certificate is post-quantum
(**ML-DSA-44 / Dilithium2**), which the system OpenSSL 3.2 cannot mint, so a helper script
compiles and runs `scripts/gen_mldsa_cert.c` against the host wolfSSL you built in step 6:

```
scripts/gen_certs.sh
```

This regenerates, in one step: `host/certs/server-key.pem` (private, gitignored),
`host/certs/server-cert.{pem,der}`, and the firmware's trust anchor `boot/certs.h` (the DER
embedded as `server_cert_der[]`). Then rebuild the firmware so it embeds the new cert:

```
cd boot && make clean && make && cd ..
```

(Skipping this step and reusing the tracked cert won't work — its private key isn't in the
repo, so the server can never prove possession of it during the handshake. `gen_certs.sh`
requires the host wolfSSL from step 6, built with `--enable-dilithium --enable-certgen
--enable-keygen`.)

## 8. Build the host DTLS server

```
cd host
gcc server_dtls.c -I wolfssl -L wolfssl/src/.libs -lwolfssl \
    -Wl,-rpath,"$PWD/wolfssl/src/.libs" -o server_dtls
cd ..
```

(`scripts/run_phaseB.sh`, below, does this step for you automatically on every run.)

## 9. Run it

### Option A — full DTLS 1.3 handshake (recommended)

```
bash scripts/run_phaseB.sh
```

Run this directly in your terminal, not through anything that isn't an interactive TTY — it
calls `sudo -v` up front, which needs a real password prompt to succeed. It caches your `sudo`
credentials, brings up `tap0` (192.168.1.100/24),
(re)builds and starts `host/server_dtls`, starts `tcpdump`, launches `litex_sim` with
`--ram-init=boot/boot.bin`, waits for `HANDSHAKE COMPLETE` + the app-data round trip, tears
everything down, and prints the server log / sim UART tail / pcap summary. Evidence lands in
`evidence/phaseB_*`.

Look for, on the sim UART: `*** DTLS 1.3 HANDSHAKE COMPLETE ***` / cipher
`TLS13-AES128-GCM-SHA256` / group `ML_KEM_512`; on the server: `*** DTLS 1.3 handshake
complete ***` with the same cipher/group.

### Option B — manual, step by step (for understanding or debugging)

```
sudo ip tuntap add mode tap tap0
sudo ip addr add 192.168.1.100/24 dev tap0
sudo ip link set dev tap0 up

cd host && ./server_dtls 192.168.1.100 &   # listens on 192.168.1.100:11111
cd ..

litex-env/bin/litex_sim $FLAGS --ram-init=boot/boot.bin   # $FLAGS from step 4; self-sudos to open tap0
```

### Option C — headless bench (no sudo, no tap, no ethernet)

For iterating on crypto/DTLS code without networking:

```
bash scripts/run_bench.sh [timeout_seconds]
```

Runs a dummy-IO build of the firmware in `litex_sim` to exercise `wolfSSL_connect()` up to
(but not past) the point it needs real IO — useful for catching CPU traps or build issues fast.

## 10. Reproduce the benchmark numbers

```
scripts/footprint.sh          # prints a Markdown row: .text/.rodata/.data/.bss, boot.bin size, handshake latency
scripts/footprint.sh -a       # also appends a machine-readable row to evidence/footprint.csv
```

Reads `boot/boot.elf` via `riscv64-linux-gnu-size -A` and scrapes handshake latency from the
newest `evidence/phaseB_sim.log`. See `BENCHMARKS.md` for the current numbers and how to read
them.

To reproduce the **ML-DSA-44 transport benchmark** (the controlled 2×2 matrix that selected the
interrupt-RX / `nrxslots=2` production config over polling + server pacing):

```
bash scripts/run_mldsa_bench.sh    # needs sudo/tty; drives all four cells
```

It rebuilds the firmware per cell, compiles the Verilator model once per RX-slot count, runs
each cell, and writes `evidence/mldsa_bench.csv` plus per-cell logs/pcaps. See `BENCHMARKS.md`
§ ML-DSA-44 for the results and the cell-B1 selection.

## Troubleshooting

- **`ModuleNotFoundError: litex`** — the venv's baked-in absolute path no longer matches; you
  moved/renamed the project directory after setup. Re-run `./litex_setup.py --init --install`
  from the new location, or move it back.
- **Firmware hangs with no UART output past a certain point** — almost always the unaligned
  access trap (see step 5); confirm `WOLFSSL_USE_ALIGN` and `-mstrict-align` are both present
  and rebuild with `make clean`.
- **`ethmac` count is 0 in generated headers, or CSRs look wrong** — the `build/sim` header
  generation run and the firmware-loading run used different flags. Regenerate (step 4) and
  rebuild firmware with the exact same `$FLAGS`.
- **CFLAGS/`user_settings.h` changes don't seem to take effect** — `boot/Makefile`'s `.o`
  files don't rebuild on a flag-only change; `make clean` first.
- **Handshake stalls with the server retransmitting a flight indefinitely** — the simulated
  liteeth MAC has few RX slots and can drop the tail of a fast burst while the ~1 MHz client
  is busy in crypto. The default firmware handles this with **interrupt-driven RX**
  (`boot/Makefile` `DTLS_RX_IRQ ?= 1`): the ethmac ISR drains the MAC even mid-crypto, so no
  server pacing is needed (`server_dtls` defaults to `DTLS_PACE_US=0`). If you build the polled
  firmware instead (`make DTLS_RX_IRQ=0`), re-enable pacing on the server —
  `DTLS_PACE_US=25000 ./server_dtls 192.168.1.100` — or it will stall as above.
