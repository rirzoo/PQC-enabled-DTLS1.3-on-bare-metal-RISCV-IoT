# PQC-DTLS 1.3 on Bare-Metal RISC-V

Post-quantum-secured **DTLS 1.3** between a Linux server and a bare-metal **RISC-V** IoT
device, simulated with **LiteX + Verilator**. The goal is a constrained embedded target
(VexRiscv, simulated RAM/flash-scale footprint) speaking DTLS 1.3 with a post-quantum key
exchange, so the crypto and transport work is representative of a real IoT deployment rather
than a desktop-class TLS stack.

## Architecture

- **Firmware target:** VexRiscv `standard` (rv32im, no hardware unaligned access) running
  under `litex_sim`, with a LiteX-generated SoC (liteeth for the network device).
- **Firmware stack:** `boot/` — a bare-metal C client linking vendored wolfSSL/wolfCrypt
  directly against the liteeth UDP driver (no OS, no sockets layer).
- **Host stack:** `host/` — a Linux DTLS 1.3 server built against a locally-compiled wolfSSL,
  reachable from the simulated SoC over a `tap0` bridge (real Linux networking, not a mocked
  transport).
- **Transport:** UDP/DTLS over `tap0`, so packet captures (`evidence/*.pcap`) reflect what
  would actually appear on the wire.

## Current capabilities

- End-to-end **DTLS 1.3 handshake** completes between the simulated firmware and the host
  server over the real UDP transport.
- **ML-KEM-512** post-quantum key exchange, negotiated and verified on both ends.
- **ECC P-256** classical server-certificate authentication (client verifies the server; not
  yet post-quantum-authenticated, and not yet mutual auth).
- **AES-128-GCM / SHA-256** record protection, HKDF key schedule.
- Firmware footprint and handshake-latency numbers are tracked per capability set — see
  **[BENCHMARKS.md](BENCHMARKS.md)**.

## Where it's headed

Post-quantum authentication (Dilithium/ML-DSA, then mutual auth), session resumption, and
footprint/latency optimization once the feature set is locked in — see
**[ROADMAP.md](ROADMAP.md)** for the full picture.

## Getting started

Setting up LiteX/Verilator, building the firmware and host server, and running the simulated
handshake is a multi-step process with a few non-obvious gotchas (toolchain choice, flag
consistency between SoC generation and firmware load, an unaligned-access trap on this CPU
variant). See **[SETUP.md](SETUP.md)** for the full step-by-step guide.
