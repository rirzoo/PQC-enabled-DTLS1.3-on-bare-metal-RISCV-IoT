# PQC-DTLS 1.3 on Bare-Metal RISC-V — Benchmarks

Firmware footprint and handshake-latency results for the **client firmware**
(`boot/boot.elf` → `boot/boot.bin`), tracked against **what the built client can actually do**
— the capability set it supports at that point, not the raw list of build flags. Each
capability change (e.g. adding Dilithium mutual auth, or trimming unused algorithms) gets a
new row so the delta is attributable to a capability, not lost in noise.

---

## Firmware footprint tracking

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
| 2026-08-30 | **DTLS 1.3 client · ML-KEM-512 KEM · ECC P-256 server-auth verify · AES-128-GCM/SHA-256** — Phase B (B6) | 505,196 | 82,624 | 552 | 12,848 | 588,392 B (575 KiB) | 47,456 ms |

Handshake latency captured on the instrumented re-run (`evidence/phaseB_sim.log`, first
machine-readable row in `evidence/footprint.csv`): **47,456 ms sim-time** — end to end from
`wolfSSL_connect()` start to `HANDSHAKE COMPLETE`, **inclusive of transport + the 25 ms server
inter-datagram pacing** (`DTLS_PACE_US` default). This is the transport-inclusive baseline to
beat with session resumption; it is **not** a pure-compute number (per-op cycle counters are a
separate latency-optimization item). Compare future rows only under the same pacing/loss.
(Section sizes shifted a few dozen bytes vs. the pre-latency B6 image because the
instrumentation added the `dtls_uptime_millis()` call sites.)

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
