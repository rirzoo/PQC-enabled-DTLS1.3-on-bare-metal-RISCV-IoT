# PQC-DTLS 1.3 on Bare-Metal RISC-V — Progress & Evidence Log

Running record of what works (concise) and what doesn't (detailed), with sanitized
evidence, for proof-of-work and cross-session tracking. Newest entries at the top of each
phase. Captures/screenshots live under `evidence/`.

Legend: ✅ success · ❌ failure/blocker (resolved) · ⏳ open

---

## Firmware footprint tracking

Memory footprint of the **client firmware** (`boot/boot.elf` → `boot/boot.bin`), tracked
against **what the built client can actually do** — the capability set it supports at that
point, not the raw list of build flags. Each capability change (e.g. adding Dilithium mutual
auth, or trimming unused algorithms) gets a new row so the delta is attributable to a
capability, not lost in noise.

**How to reproduce a row:** run **`scripts/footprint.sh`** — it reads `.text/.rodata/.data/
.bss` from `<triple>-size -A boot/boot.elf`, the loadable image from `stat -c%s boot/boot.bin`,
and scrapes the handshake latency from the newest `evidence/phaseB_sim.log`
(`handshake time: N ms`, printed by the firmware). It prints a ready-to-paste Markdown row;
`-a` also appends a machine-readable row to `evidence/footprint.csv`. (Manual equivalent:
`riscv64-linux-gnu-size -A boot/boot.elf`.) Ignore the Berkeley (`size` without `-A`) `bss`
total: it folds in `._user_heap` (a fixed **16 MiB** static heap pool, not capability-scaled)
and so overstates RAM by ~16 MB.

**Handshake latency** is measured in **sim-time milliseconds** (firmware `dtls_uptime_millis()`,
a 1 MHz tick source), so it is deterministic regardless of host/Verilator speed. It is
*transport-inclusive* — it covers DTLS round-trips, retransmits, and the server's flight
pacing (`DTLS_PACE_US`, default 25 ms) — so it is only comparable across runs with the **same
pacing and loss conditions**. Hold those constant when comparing optimizations.

| Date | Client can do | .text | .rodata | .data | .bss | Loadable `boot.bin` | Handshake |
|------|---------------|------:|--------:|------:|-----:|--------------------:|----------:|
| 2026-08-30 | UDP only (no wolfSSL linked) — Phase A | 9,800 | — | 16 | 512,360 | 9,816 B | n/a |
| 2026-08-30 | **DTLS 1.3 client · ML-KEM-512 KEM · ECC P-256 server-auth verify · AES-128-GCM/SHA-256** — Phase B (B6) | 505,116 | 82,560 | 552 | 12,848 | 588,264 B (574 KiB) | pending re-run* |

\* B6 completed the handshake but predates the latency instrumentation; the `handshake time`
line lands on the next tap run and this cell (plus `evidence/footprint.csv`) gets filled then.

**Baseline (B6) capability detail.** What this image supports today: a **DTLS 1.3 client**
(RFC 9147) over the liteeth UDP transport; **ML-KEM-512** post-quantum key exchange;
**ECC P-256** server-certificate verification (classical server auth); **AES-128-GCM** with
**SHA-256** record protection; HKDF key schedule; SHA-2 + SHA-3/SHAKE (the latter for ML-KEM).
Code cost vs. the UDP-only firmware: **+495 KiB** `.text`, and `.rodata` appears (+80 KiB) —
i.e. linking wolfSSL + wolfCrypt for this capability set costs ~575 KiB of ROM image.

**Not yet in the image (future rows will move these):**
- **Dilithium / ML-DSA mutual auth** — *not compiled in* (no `HAVE_DILITHIUM`). Adding it is
  the next capability and expected to be the single largest footprint jump.
- **Session resumption / tickets** — not enabled.
- **Trim candidates** — RSA, Ed25519, X25519/Curve25519 are compiled in but **not exercised**
  by the ML-KEM + ECC-P256 path; removing them is a footprint-reduction lever (a future row
  should show ROM shrinking with no capability lost).

---

## Phase A — Restore UDP transport

### ✅ A2 — Bidirectional UDP transport verified on the wire (Phase A DONE)
**Date:** 2026-08-30 · runner: `scripts/run_phaseA.sh` · evidence: `evidence/phaseA_*`

Full round trip confirmed both in the sim UART and in the packet capture
(`evidence/phaseA_udp.pcap`):
```
ARP Request  who-has 192.168.1.100 tell 192.168.1.50
ARP Reply    192.168.1.100 is-at aa:b6:24:69:77:21
IP 192.168.1.50.1234  > 192.168.1.100.1234: UDP, length 21   # "Hello from LiteX SoC!"
IP 192.168.1.100.1234 > 192.168.1.50.1234:  UDP, length 24   # "Hi I am the linux server"
```
Sim UART: `Ethernet init... / Local IP: 192.168.1.50 / ... Server resolved. / Message
sent. / [UDP RX] Received 24 bytes from c0a80164:1234 / Hi I am the linux server`.
(c0a80164 = 192.168.1.100.) ARP + both UDP directions OK.

Minor: `evidence/phaseA_server.log` came out empty — the C server's `stdout` is
block-buffered to a file and it was killed before flush; it clearly ran (SoC got its
reply). Fix when rewriting the server for DTLS: `setvbuf(stdout, NULL, _IONBF, 0)`.

**Phase A exit criterion met.** Transport restored.

### ✅ A1 — Rebuilt SoC (with Ethernet) + firmware + server; transport run pending
**Date:** 2026-08-30

- Regenerated `build/sim` with the canonical flags **incl. `--with-ethernet`** (venv
  activated so the BIOS Makefile's `python3 -m litex...` resolves):
  `ethmac` count in `build/sim/.../generated/csr.h` went `0 → 65`; `bios.bin` and the
  Verilator `Vsim` both built. (The run's only failure was the final `sudo obj_dir/Vsim`
  — no tty — which is the user's step.)
- Rebuilt firmware `cd boot && make clean && make`: links cleanly now that `udp.c`'s
  `#ifdef CSR_ETHMAC_BASE` block is active. Artifacts: `boot/boot.bin` (9,816 B).
  **Footprint baseline (UDP-only firmware, no wolfSSL linked):**
  `text 9800 · data 16 · bss 512360` (`riscv64-linux-gnu-size boot.elf`).
  `udp_start/udp_send/udp_service/main` resolve at `0x40000xxx` (RAM).
- Built host UDP echo server: `host/server`.
- **Pending:** run `scripts/run_phaseA.sh` (needs sudo: tap0 + tcpdump + sim self-sudo) to
  confirm bidirectional datagrams SoC(192.168.1.50)↔server(192.168.1.100) on `tap0`.
  Evidence will land in `evidence/phaseA_*` (server log, sim UART log, `phaseA_udp.pcap`).

**Note for the shim (Phase B):** `libliteeth`'s RX callback passes a pointer into the
hardware RX slot, valid only for the callback's duration; `udp_send` sets IP
DONT_FRAGMENT (no IP fragmentation) so each datagram must fit one Ethernet frame — the
DTLS IO shim must copy each datagram out on arrival and cap wolfSSL's DTLS MTU accordingly.

### ✅ A0 — Root-caused and fixed the "nothing works anymore" environment breakage
**Date:** 2026-08-30

**Symptom.** The project no longer builds or simulates after previously working.

**Investigation & findings (two independent root causes):**

1. **Project directory was renamed, breaking every baked-in absolute path.** LiteX bakes
   absolute paths throughout the build and venv. Evidence:
   - `build/sim/software/include/generated/variables.mak` (included by `boot/Makefile`)
     pins `SOC_DIRECTORY`, `PACKAGE_DIRS`, `BUILDINC_DIRECTORY`, … to
     `/home/romir/1_PQC_DTLS_PS/Constraint_Env_Sim` — the *old* path.
   - The venv (`litex-env`) had 26 PEP-660 editable-install finder files
     (`__editable___*_finder.py`) whose `MAPPING` pointed at the old path, e.g.
     `{'litex': '/home/romir/1_PQC_DTLS_PS/Constraint_Env_Sim/litex/litex'}`, plus every
     `litex-env/bin/*` wrapper shebang (`#!/home/romir/1_PQC_DTLS_PS/.../python3`).
   - Result: `litex-env/bin/litex_sim` failed with `ModuleNotFoundError: No module named
     'litex'`, and `make` in `boot/` would fail to find the SoC dirs. The dir had also
     been moved under a path *containing spaces* (`.../PQC Enabled DTLS 1.3 on Bare Metal
     RISC-V/...`), which independently breaks Verilator/Make.

   **Fix (approved):** moved the project back to its original no-space path
   `/home/romir/1_PQC_DTLS_PS/Constraint_Env_Sim`. All baked-in paths and venv shebangs
   then resolved with zero further edits.
   **Evidence after fix:**
   ```
   $ litex-env/bin/python3 -c "import litex, liteeth, migen; print('imports OK')"
   imports OK
   $ litex-env/bin/litex_sim --help   # runs (prints usage)
   ```

2. **SoC/firmware Ethernet mismatch.** Committed `csr.json` contains `ethmac`, but the
   firmware's generated headers (`build/sim/.../generated/csr.h`) had **zero** `eth`
   symbols — i.e. `build/sim` was last regenerated by a `litex_sim` run *without*
   `--with-ethernet` (it regenerates from CLI flags each run). The `libliteeth` UDP driver
   was thus targeting hardware absent from the SoC. **Fix in progress:** regenerate
   `build/sim` with `--with-ethernet` (canonical flag set below), then rebuild firmware.
   Also note a secondary flag drift: `build/sim` was built `--cpu-variant=standard` while
   `NOTES.md` ran the sim with `--cpu-variant=full`; flags must match between the generate
   and run invocations.

**Lesson (recorded in `CLAUDE.md`):** never rename/move the project dir; keep it at a
no-space path; keep all `litex_sim` flags identical between the header-generating run and
the firmware-loading run.

---

## Phase B — Minimal DTLS 1.3 handshake (ML-KEM, classical certs)

### ✅ B6 — End-to-end DTLS 1.3 handshake completes with ML-KEM-512 (Phase B exit criterion met)
**Date:** 2026-08-30 · evidence: `evidence/phaseB_{sim,server,tcpdump}.log`, `evidence/phaseB_dtls.pcap`

**Symptom.** With the B5 alignment fix in place, the full end-to-end tap run now gets
*much* further — **no CPU trap through the entire exchange** — but never completes:
- Firmware UART: ClientHello → HRR → ClientHello+cookie → receives ServerHello/Cert
  fragments, then loops sending 40-byte ACKs until `FAIL: handshake timed out after 60 s`.
- Server: `Client hello from 192.168.1.50:5555` then `accept failed: err -308
  (error state on socket)` (wolfSSL DTLS retransmits its flight to exhaustion, then gives up).

**Root cause (confirmed from pcap + UART).** The server flushes its whole handshake flight
as a **burst of 5 datagrams within ~1 ms** (`847, 74, 500, 108, 66` bytes = ServerHello,
EncryptedExtensions, Certificate(frag), CertVerify, Finished). The RISC-V client runs in a
~1 MHz Verilator sim with a liteeth MAC that has only a couple of hardware RX slots, and it
is busy in ML-KEM/cert crypto when the burst lands — so the **tail of each burst is dropped
in hardware** before `udp_service()` can drain it. Decisive evidence — client RX-size
histogram over the whole run (`grep '[io] RX' phaseB_sim.log`):
`847×3, 500×6, 144×2 (HRR), 74×1, 108×1, **66×0**`. The 66-byte record (the server's
**Finished**) is *never* delivered, so the client can never complete; it ACKs what it has,
the server retransmits the unacked tail `{500,108,66}` indefinitely, and `accept` times out.
The 8-slot software ring in `dtls_io.c` is not the bottleneck — the loss is upstream in the
MAC. (This is a bare-metal-vs-desktop transport mismatch, **not** a crypto or config bug:
the ClientHello, HRR/cookie, key shares, ServerHello and record layer all work.)

**Fix (host-only, this pass).** Pace the server's flight: a custom wolfSSL send callback
(`PacedSend` in `host/server_dtls.c`, installed via `wolfSSL_CTX_SetIOSend`) `nanosleep`s
between datagrams so the slow client can drain each into its ring. Delay defaults to 25 ms,
tunable at runtime via the `DTLS_PACE_US` env var (adds ~125 ms to a 5-packet flight —
negligible against DTLS's ~1 s retransmit timers). If pacing proves insufficient, the
fallback is a SoC-side bump of liteeth `nrxslots` (regen `build/sim` + rebuild firmware).
**Status:** fix built (`host/server_dtls`); awaiting a tap re-run to confirm completion.

**Regression from the first pacing attempt (2026-08-30, re-run).** The initial `PacedSend`
did its own `send(*(int *)ctx, …)`, assuming `ctx` was `&ssl->wfd` (true for *TLS* over a
connected socket). For **DTLS** the write context is a `WOLFSSL_DTLS_CTX` struct (peer
sockaddr + rfd/wfd), so `*(int *)ctx` read the sockaddr *size* field (~16) instead of the
fd; `send(16, …)` returned −1 → `WOLFSSL_CBIO_ERR_GENERAL` → `accept` failed `-308`
**before emitting a single datagram**. Decisive evidence — re-run pcap
(`tcpdump -r evidence/phaseB_dtls.pcap`): ARP resolves, client sends ClientHello (971 B) and
retransmits it 6× with backoff, and the **server never replies at all** (contrast the prior
run's `847/74/500/108/66` flight). The `recvfrom(peek): Resource temporarily unavailable`
spam in `phaseB_server.log` is downstream noise — after `accept` bails the socket is still
`connect()`ed to the peer and the blocking `recvfrom` just times out (EAGAIN) each loop.
**Fix:** `PacedSend` now delegates to wolfSSL's own `EmbedSendTo(ssl, buf, sz, ctx)` (correct
DTLS addressing) and only adds the `nanosleep` afterwards. Rebuilt; awaiting a clean re-run.

**✅ Resolved — handshake completes (2026-08-30, clean re-run).** With the corrected
`PacedSend`, the full end-to-end tap run **succeeds**. The 66-byte Finished record now
reaches the client (`[io] RX 66 bytes -> wolfSSL` in `phaseB_sim.log` — absent in every
prior run), confirming the burst-overrun was the real blocker and pacing resolves it.
Negotiated parameters agree on both ends:
- **Server** (`phaseB_server.log`): `*** DTLS 1.3 handshake complete ***` · version `DTLSv1.3`
  · cipher `TLS_AES_128_GCM_SHA256` · **group `ML_KEM_512`** · `client says: Hello from
  RISC-V DTLS 1.3 client!` → replied.
- **Client/sim** (`phaseB_sim.log`): `*** DTLS 1.3 HANDSHAKE COMPLETE ***` · cipher
  `TLS13-AES128-GCM-SHA256` · **group `ML_KEM_512`** · `server says: Hello from PQC-DTLS 1.3
  Linux server!`

This meets the **Phase B exit criterion**: DTLS 1.3 handshake end-to-end over the liteeth
UDP transport, ML-KEM-512 KEM negotiated (visible in the `key_share`/HRR exchange and logged
as the group on both ends), server authenticated with the ECC P-256 cert, and one app-data
message round-tripping each way. PQC posture this pass: ML-KEM-512 key exchange + classical
ECC cert auth (Dilithium/ML-DSA mutual auth deferred to a later pass). Evidence:
`evidence/phaseB_{sim,server,tcpdump}.log`, `evidence/phaseB_dtls.pcap`.

### ✅ B5 — Root-caused & fixed the ClientHello "hang": unaligned-access CPU trap
**Date:** 2026-08-30 · evidence: `evidence/bench_*.log` · bench: `scripts/run_bench.sh`

**Symptom.** After B4 (debug disabled, heap/stack enlarged), the firmware still froze
inside `wolfSSL_connect` while building the DTLS 1.3 ClientHello — no datagram on the wire,
no further UART. Reproduced deterministically.

**How it was diagnosed.** Built a **headless, no-ethernet, no-sudo** litex_sim bench
(`boot/main.c` bench variant: dummy IO callbacks, ML-KEM key share, `wolfSSL_connect`) so
the crypto/DTLS path could be iterated fast without tap/root. Compiled **printf milestone
markers directly into the wolfSSL sources** (they are part of the firmware build) and
bisected: `SendTls13ClientHello` → `Dtls13HandshakeSend` → `Dtls13SendOneFragmentRtx` →
`Dtls13SendFragment` → `Dtls13RlAddPlaintextHeader`, stalling on a single store. A custom
`mtvec` trap handler in the bench then printed the exact fault:
```
*** EXC mcause=6 mepc=4004ea78 mtval=4008f81a ***
```
`mcause=6` = **store-address-misaligned**; `mtval=0x4008f81a` (out+7, the 6-byte DTLS record
sequence-number field) is not 4-aligned. `objdump` at mepc: `sw a0,7(s0)`.

**Root cause.** VexRiscv `--cpu-variant=standard` (rv32im) has **no hardware unaligned
load/store**. wolfSSL's `c32toa`/`c16toa`/`ato32` (`wolfcrypt/src/misc.c`) default to
`*(word32*)c = ByteReverseWord32(x)` — an explicit unaligned word store. The generic LiteX
`isr()` (`libbase/isr.c`) services only interrupts, ignores CPU exceptions, and does **not**
advance `mepc`, so `mret` re-runs the faulting store forever → an infinite trap loop that
presents as a hang. (This is the "building wolfSSL for a 32-bit arch" class of problem the
referenced blog hinted at.)

**Fix (both, `make clean` required):**
1. `#define WOLFSSL_USE_ALIGN` in `user_settings.h` → byte-wise conversion macros.
2. `CFLAGS += -mstrict-align` in `boot/Makefile` → stops GCC merging byte writes into
   unaligned word/halfword stores. (Alone, `-mstrict-align` fixed the compiler merges but
   NOT the explicit `*(word32*)` cast — `WOLFSSL_USE_ALIGN` is the one that removes that.)

**Verified (bench, `evidence/bench_align.log`):**
```
[G3] RlAddPlaintext done → [F2] before SendBuffered →   [send] 971 bytes
[F3] after SendBuffered ret=0 … [M8] after Dtls13HandshakeSend ret=0
wolfSSL_connect took ~826 ms sim-time
connect ret=-1 err=2 (WANT_READ)   # ClientHello fully built + handed to IO; no reply in bench
```
The 971-byte ClientHello (ML-KEM-512 key share) is constructed and emitted with no trap;
`WANT_READ` is the correct non-blocking result with dummy IO. ML-KEM keygen inside connect
works (~0.8 s sim-time total). Fragmentation/MTU were ruled out (hang reproduced identically
at MTU 4096, i.e. single-datagram, no fragmentation).

**Reusable diagnostic left in tree:** `scripts/run_bench.sh` (headless bench runner) and the
`my_trap` mtvec handler pattern (catches future unaligned/access faults instead of hanging).

### ✅ B1 — Host-to-host DTLS 1.3 + ML-KEM-512 validated (crypto/config de-risked)
**Date:** 2026-08-30 · evidence: `evidence/` (see below) · script:
`scratchpad/dtls_hosttest.sh`

Built wolfSSL 5.8.4 on the host and ran its example DTLS server↔client over UDP loopback
with our self-signed ECC P-256 cert. **Success:**
```
SSL version is DTLSv1.3
SSL cipher suite is TLS_AES_256_GCM_SHA384
SSL curve name is ML_KEM_512          # PQC KEM negotiated
Client message: hello wolfssl!        # app data both ways; client exit=0
```
This proves the cert, ML-KEM-512 key exchange, ClientHello fragmentation, and CA
verification all work together in DTLS 1.3. The firmware must replicate the **client**.

**Build recipe that works (host wolfSSL):**
`./configure --enable-tls13 --enable-dtls --enable-dtls13 --enable-dtls-frag-ch
--enable-mlkem --enable-experimental --enable-curve25519 --enable-ed25519 && make`
- `--enable-dtls13` alone is rejected → must also pass `--enable-dtls --enable-tls13`.
- **`--enable-dtls-frag-ch` is REQUIRED** for DTLS 1.3 + PQC: the (large) ClientHello
  carrying the ML-KEM key share must be fragmented across datagrams, else the handshake
  fails. Same flag as firmware's `WOLFSSL_DTLS_CH_FRAG`. (wolfSSL even emits a build
  `#warning` about this, which `-Werror` turned into the initial build failure.)

Run flags: server `-v 4 -u --pqc ML_KEM_512 -c cert.pem -k key.pem`; client
`-v 4 -u -A cert.pem --pqc ML_KEM_512`.

**Certs:** `host/certs/server-{key,cert}.pem` (ECC P-256, CN/SAN=192.168.1.100),
`server-cert.der` embedded into firmware as trusted CA at `boot/certs.h`
(`server_cert_der[457]`).

**Config note:** `NO_ASN_TIME` means wolfSSL does not define `LowResTimer()`; the firmware
must supply `word32 LowResTimer(void)` (elapsed seconds) — planned via RISC-V `rdcycle` /
`CONFIG_CLOCK_FREQUENCY` (=1 MHz). Firmware timer0 has only load/reload/en/value (a
down-counter, no uptime CSR).

### ❌→🔧 B4 — First tap run: firmware never transmits ClientHello (verbose-debug stall)
**Date:** 2026-08-30 · evidence: `evidence/phaseB_{sim.log,server.log,dtls.pcap}` (run 1)

**Symptom.** Firmware booted, resolved ARP, created the DTLS ctx, loaded the embedded
CA (issuer/subject parsed correctly), created the SSL, and began the handshake — the UART
log shows it building the ClientHello through `Key Share extension to write` →
`Dtls13RtxNewRecord`, then **total silence for ~8 min** until the run was torn down. The
server log shows only "listening" (no "Client hello"), and **the pcap contains the ARP
exchange but NOT a single DTLS/UDP datagram from 192.168.1.50** — the ClientHello never
reached the wire.

**Diagnosis.** `DEBUG_WOLFSSL_VERBOSE` makes every crypto step emit UART lines; on the
1 MHz Verilator sim each line is a slow write, so ML-KEM-512 keygen + ClientHello
construction (hundreds of debug lines) had not even finished building/sending the first
flight within the 8-min window. It was crawling, not (confirmed) crashed — but the debug
logging alone made an end-to-end run impractical, and left no visibility into the actual
send path (wolfSSL's own send is replaced by our silent callback).

**Fixes applied (rebuild pending):**
1. Disabled `DEBUG_WOLFSSL` / `SHOW_GEN` / `DEBUG_WOLFSSL_VERBOSE` in `user_settings.h`
   (re-enable one line to debug). Cuts UART traffic ~100x.
2. Enlarged firmware heap 500K→16M and stack 500K→2M in `boot/linker.ld` (RAM is 100 MB;
   `WOLFSSL_SMALL_STACK` moves big ML-KEM/DTLS buffers to the heap) — removes heap
   exhaustion as a variable.
3. Instrumented the IO path: `dtls_io.c` prints `[io] TX/RX N bytes` around each datagram;
   `main.c` prints handshake `[hs]` error transitions + a periodic alive heartbeat. Next
   run will show exactly whether the ClientHello is sent, and where any stall is.
4. Widened the run window (`scripts/run_phaseB.sh`: sim timeout 1200s, poll 900s).

Confirmed harmless: ETHMAC_SLOT_SIZE=2048 (a ~1450 B DTLS frame fits one slot); MTU capped
at 1400; the send callback rejects oversize records.

### ✅ B3 — Custom DTLS server validated; firmware DTLS client builds; tap run pending
**Date:** 2026-08-30

- **Custom host server** `host/server_dtls.c` (DTLS 1.3, ECC cert, ML-KEM) validated
  loopback against the wolfSSL example client:
  `handshake complete / DTLSv1.3 / TLS_AES_256_GCM_SHA384 / ML_KEM_512 / client says:
  hello wolfssl!` and the client received the server's reply (exit 0). Uses the
  peek-peer→`connect()` DTLS accept pattern; `setvbuf` unbuffered so logs survive.
- **Firmware** (`boot/main.c` DTLS 1.3 client + `boot/dtls_io.c` shim) compiles and links:
  `text 655636 · data 552 · bss 524896` (~1.18 MB, fits 100 MB RAM). Symbols present:
  `wolfSSL_connect`, `wc_MlKemKey_MakeKey`, `dtls_io_init`, `LowResTimer`, `main`.
- **Build fixes discovered (all recorded in `user_settings.h`):** enabling `WOLFSSL_DTLS`
  activates `wolfio.h` typedefs that need `ssize_t` → `#include <sys/types.h>` at top of
  user_settings (picolibc provides it); `wolfSSL_dtls_set_mtu` is gated by
  `WOLFSSL_DTLS_MTU`; and `dtls.c` hard-`#error`s without `WOLFSSL_SEND_HRR_COOKIE` when
  DTLS 1.3 is compiled (server cookie code shares the TU) even for a client build.
- **Pending:** `scripts/run_phaseB.sh` (needs sudo) to run the firmware client against
  `server_dtls` over tap0 and capture the on-wire DTLS 1.3 + ML-KEM handshake.

### ✅ B2 — Firmware DTLS client + wolfSSL↔liteeth IO shim written
**Date:** 2026-08-30

- `boot/dtls_io.{c,h}`: whole-datagram RX ring fed by the liteeth `udp_service` callback
  (copies each datagram out immediately — HW slot pointer is transient); wolfSSL
  send/recv callbacks (recv returns one datagram or `WANT_READ`; send → one UDP datagram);
  `LowResTimer()` backed by a free-running LiteX **timer0** down-counter (load/reload =
  0xFFFFFFFF, wrap-accumulated), converted to seconds via `CONFIG_CLOCK_FREQUENCY` (1 MHz).
- `boot/main.c`: DTLS 1.3 client — ARP-resolves the server, `wolfDTLSv1_3_client_method`,
  loads embedded CA (`certs.h`), `wolfSSL_UseKeyShare(ML_KEM_512)`, non-blocking handshake
  loop with `dtls_get_current_timeout`/`dtls_got_timeout` retransmit, then one app-data
  exchange. Interim entropy = timer-seeded LCG (placeholder for a real TRNG).
- `boot/Makefile`: added `dtls_io.o` (boot/ root isn't in the globbed `src/`).
