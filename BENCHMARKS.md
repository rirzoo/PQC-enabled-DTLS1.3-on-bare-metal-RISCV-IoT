# PQC-DTLS 1.3 on Bare-Metal RISC-V — Benchmarks

Firmware footprint and handshake-latency results for the **client firmware**
(`boot/boot.elf` → `boot/boot.bin`), tracked against **what the built client can actually do**
— the capability set it supports at that point, not the raw list of build flags.

Each PQC capability (**ML-KEM** key exchange ✅, **ML-DSA** authentication ✅, **session
resumption** — next) gets its own section and table below, since each is measured differently;
they consolidate into one cross-feature table once all three land. `evidence/*.csv` keeps every
raw row.

**Selection policy (feature-building phase):** among passing configurations, pick the **lowest
handshake latency**, ties broken by **memory** (firmware image, then SoC RX SRAM). This may shift
toward memory once we enter pure optimization; the logged cells let us re-pick then without
re-running.

---

## § Baseline — ML-KEM-512 key exchange + ECC P-256 auth (Phase B / B6)

**Reproduce:** `scripts/footprint.sh` (see SETUP.md § 10 for how to run it and append CSV rows).
When reading the table, ignore the Berkeley (`size` without `-A`) `bss` total: it folds in
`._user_heap` (a fixed **16 MiB** static pool, not capability-scaled) and so overstates RAM by
~16 MB.

**Handshake latency** is **sim-time milliseconds** (firmware `dtls_uptime_millis()`, a 1 MHz tick),
deterministic regardless of host/Verilator speed. It is *transport-inclusive* (round-trips,
retransmits, and any server flight pacing), so it is only comparable across runs with the **same
pacing and loss conditions**.

| Date | Client can do | .text | .rodata | .data | .bss | Loadable `boot.bin` | Handshake |
|------|---------------|------:|--------:|------:|-----:|--------------------:|----------:|
| 2026-08-30 | UDP only (no wolfSSL linked) — Phase A | 9,800 | — | 16 | 512,360 | 9,816 B | n/a |
| 2026-08-30 | **DTLS 1.3 client · ML-KEM-512 KEM · ECC P-256 server-auth verify · AES-128-GCM/SHA-256** — Phase B (B6) | 505,196 | 82,624 | 552 | 12,848 | 588,392 B (575 KiB) | 47,456 ms |

The **47,456 ms** figure is the transport-inclusive baseline — end to end from `wolfSSL_connect()`
to `HANDSHAKE COMPLETE`, **inclusive of the 25 ms server inter-datagram pacing**
(`DTLS_PACE_US` default). It is **not** a pure-compute number. The ML-DSA work below removes that
pacing entirely (interrupt-driven RX), so the two sections' latencies are **not** directly
comparable — the point of the ML-DSA matrix is to measure the pacing→interrupt improvement.

---

## § ML-DSA-44 authentication (Dilithium2) — *server-auth, this pass*

Replaces the classical ECC P-256 server cert with a self-signed **ML-DSA-44 (Dilithium2)** cert
that the firmware client verifies (`WOLFSSL_DILITHIUM_VERIFY_ONLY` — the client never signs), so
the handshake is now **post-quantum-authenticated** (ML-KEM-512 KEM + ML-DSA-44 auth). Client
firmware config: `boot/wolfssl/wolfcrypt/user_settings.h`; the cert is minted by
`scripts/gen_certs.sh` (SETUP.md § 7). Cert size jumps from ~0.4 KB (ECC) to **3,996 B DER**.

**The transport lever.** ML-DSA's larger Certificate/CertificateVerify flight makes the liteeth
**RX-burst** problem worse (the SoC has only 2 HW RX slots; while the CPU is busy in ML-KEM/ML-DSA
crypto nothing drains the MAC, so the burst tail overruns the slots and is dropped in hardware).
Two fixes are benchmarked head-to-head, holding ML-DSA-44 constant:
- **poll + server pacing** — today's workaround: 25 ms `DTLS_PACE_US` between server datagrams.
  Reliable, but the pacing is baked into (and inflates) the latency.
- **interrupt-driven RX** (`-DDTLS_RX_IRQ`) — the ethmac RX ISR drains the MAC into the software
  ring *even while the CPU is mid-crypto*, so no pacing is needed (`DTLS_PACE_US=0`).

crossed with the SoC RX-slot depth (`nrxslots` = 2 vs 4; each slot is 2,048 B of SoC SRAM), giving
a 2×2 matrix. `.text`/`boot.bin` are firmware ROM; `RX SRAM` is the SoC-side cost that `nrxslots`
actually buys (`nrxslots × 2,048 B`) — reported separately because the firmware ROM barely moves
with slot count.

**Reproduce:** `scripts/run_mldsa_bench.sh` (see SETUP.md § 10). It drives all four cells and
writes `evidence/mldsa_bench.csv`; the firmware bakes `ETHMAC_RX_SLOTS` into udp.c's TX-buffer
offset, so the harness rebuilds the firmware for each `nrxslots`.

| Cell | RX method | nrxslots | pacing (µs) | Handshake (ms) | .text | Loadable `boot.bin` | RX SRAM | Pass | ★ |
|------|-----------|---------:|------------:|---------------:|------:|--------------------:|--------:|:----:|:-:|
| A1 | poll (server pacing) | 2 | 25,000 | 77,538 | 519,000 | 608,304 B | 4,096 B | ✅ | |
| A2 | poll (server pacing) | 4 | 25,000 | 88,210 | 519,000 | 608,304 B | 8,192 B | ✅ | |
| B1 | interrupt (`-DDTLS_RX_IRQ`) | 2 | 0 | 69,938 | 519,136 | 608,496 B | 4,096 B | ✅ | ★ |
| B2 | interrupt (`-DDTLS_RX_IRQ`) | 4 | 0 | 69,909 | 519,136 | 608,496 B | 8,192 B | ✅ | |

_`.text`/`boot.bin` are the compile-time footprints (poll vs interrupt differ by the ISR; slot
count does not change ROM). Measured 2026-09-05 from `evidence/mldsa_bench.csv` (all four cells
PASS: full DTLS 1.3 handshake, ML-KEM-512 + ML-DSA-44 verified, app-data round-trip)._

**Readout — all four cells complete an ML-DSA-44 handshake; the two RX levers behave very
differently:**

- **Interrupt RX is insensitive to slot count.** B1 (2 slots) and B2 (4 slots) are latency-
  equivalent — **69,938 ms vs 69,909 ms** (Δ = 29 ms, 0.04%) — with *identical* packet traces (52
  server→client, 8 client→server, 3 retransmitted 847-B flights each). The ethmac ISR drains the
  MAC as datagrams arrive regardless of how many HW slots exist, so 2 slots are as good as 4. The
  extra 4 KB of RX SRAM buys nothing.
- **Polling is slot-sensitive, and *more* slots is *worse*.** A2 (4 slots, 88,210 ms) is **10.7 s
  slower** than A1 (2 slots, 77,538 ms) and does more transport work (94/28 vs 89/25 packets).
  Without an ISR, deeper slots let the client buffer and chew through more (partly redundant)
  server datagrams per poll, adding rounds.
- **Interrupt RX beats poll+pacing at every slot count** — B1/B2 (~69.9 s, 52/8 packets) vs A1
  (77.5 s, 89/25) and A2 (88.2 s, 94/28). Removing the 25 ms server pacing and draining in the ISR
  saves **8–18 s** and roughly halves the packet count.

> **Correction to an earlier interim reading.** Runs on the *pre-fix* harness showed B2 (irq, 4
> slots) as much slower than B1 (90–122 s vs ~70 s), which suggested "fewer slots is faster with
> interrupt RX." That gap was a **harness artifact** (the first cell of each `nrxslots` ran a stale
> Verilator model built for the previous slot count), not a transport effect. On the
> corrected harness B1 and B2 are packet-for-packet identical. The real finding is the opposite of
> the interim one: **with interrupt RX, slot count does not affect latency at all.**

**Selected: B1 — interrupt-driven RX, `nrxslots = 2`, no server pacing (`DTLS_PACE_US = 0`).**
Under the phase-scoped policy (lowest latency, ties broken by memory): the two interrupt cells are
latency-equivalent (the 29 ms by which B2 is nominally lower is sim-time noise — the packet traces
are identical), so latency does not separate them and the **memory tiebreak** decides. B1 uses
**half the RX SRAM of B2 (4 KB vs 8 KB)** for the same handshake, and beats both poll cells
outright. So B1 is fastest-tier *and* cheapest — the interrupt ISR, not slot depth, is what
matters, which is why the pacing-free 2-slot config wins.

**Footprint vs. the ECC baseline (already measured):** adding ML-DSA-44 verify grows `.text`
505,196 → **519,000** (+13,804 B, ~13.5 KiB) and `boot.bin` 588,392 → **608,304** (+19,912 B,
~19.4 KiB). Smaller than a full ML-DSA add would cost because `WOLFSSL_DILITHIUM_VERIFY_ONLY`
drops the keygen/sign paths (the client only verifies).

---

## § Session resumption — *next feature (placeholder)*

DTLS 1.3 tickets (`HAVE_SESSION_TICKET`, both ends). Expected to dramatically lower the *resumed*
handshake latency (no ML-KEM keygen/decap, no ML-DSA verify on the resumed path). Its own table
will be added here when implemented; the cross-feature consolidation happens only after this lands.
