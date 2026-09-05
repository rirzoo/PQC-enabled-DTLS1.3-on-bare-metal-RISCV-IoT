# Roadmap — PQC-DTLS 1.3 on Bare-Metal RISC-V

Where this project is headed. For current benchmark numbers see `BENCHMARKS.md`; the
day-to-day engineering log is kept outside version control.

## Current state

- **Phase A** — UDP transport restored over liteeth. Done.
- **Phase B** — DTLS 1.3 handshake completes end-to-end. Done. Negotiated **ML-KEM-512**
  (post-quantum key exchange), ECC P-256 server-certificate verification, **AES-128-GCM/SHA-256**
  record protection.
- **Phase C** — Post-quantum *authentication*. Done (server-auth). The ECC server cert is
  replaced by a self-signed **ML-DSA-44 (Dilithium2)** cert the client verifies, and RX is now
  interrupt-driven (production cell **B1**: `DTLS_RX_IRQ=1`, nrxslots=2, no server pacing). See
  `BENCHMARKS.md` § ML-DSA-44 for the 2×2 transport benchmark.
- **PQC posture today:** post-quantum *key exchange* (ML-KEM-512) **and** post-quantum *server
  authentication* (ML-DSA-44). Remaining classical gap: **mutual** auth (the client presents no
  cert yet).

## Next milestones

### Mutual post-quantum authentication (client ML-DSA cert)
Server-auth with ML-DSA-44 is done. Extend to mutual auth: the client also presents an ML-DSA
cert (`wolfSSL_CTX_set_verify` on the server + a client cert/key). This adds the ML-DSA *sign*
path to the firmware (today it is verify-only), so expect a further footprint jump —
`BENCHMARKS.md` will gain a capability row quantifying it.

### Session resumption
DTLS 1.3 session tickets, so a reconnect skips the full ML-KEM key exchange. Should produce
a large latency win over the current full-handshake baseline.

### Footprint optimization
Once the target feature set (ML-KEM + ML-DSA + resumption) is locked in, trim algorithms
compiled in but not exercised by the negotiated path (RSA, Ed25519, X25519, …) and right-size
the heap/stack pools — with no capability lost.

### Latency optimization
Per-operation cycle counters (ML-KEM decap, certificate verify, key schedule) for a
compute-only comparison, and tuning or removing the current server-side flight pacing that
works around the simulated NIC's limited RX capacity.

### Real entropy
Replace the current timer-seeded (non-cryptographic) RNG with a genuine hardware TRNG source,
once one is available in the simulated SoC. This is a known limitation of the current demo.

### Reporting
A packet capture of the DTLS 1.3 handshake showing the ML-KEM key share, and a footprint /
latency comparison table across capability sets, for the final technical report.

### Later: a higher security level
Once the lowest-tier parameter sets (ML-KEM-512, Dilithium2) are optimized to the max, revisit
moving up a NIST security level — e.g. ML-KEM-768 / ML-DSA-65 — for a larger security margin.
