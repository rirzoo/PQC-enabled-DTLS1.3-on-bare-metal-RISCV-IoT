/*
 * dtls_io.h - glue between wolfSSL DTLS and the LiteX/liteeth UDP transport.
 *
 * wolfSSL pulls bytes from a recv callback; liteeth pushes received datagrams
 * through a poll-driven callback (udp_service -> udp_rx callback). This shim
 * bridges the two with a small software ring of whole datagrams, provides the
 * send callback, and supplies LowResTimer() (required because NO_ASN_TIME
 * disables wolfSSL's built-in timer).
 */
#ifndef DTLS_IO_H
#define DTLS_IO_H

#include <stdint.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/ssl.h>

/* Initialise the transport shim: remembers the local/peer UDP ports, resets the
 * RX ring, starts the free-running timer, and installs the liteeth RX callback.
 * Call after udp_start() and after the peer MAC has been ARP-resolved. */
void dtls_io_init(uint16_t local_port, uint16_t peer_port);

/* Install the wolfSSL send/recv callbacks on a context. */
void dtls_io_register(WOLFSSL_CTX *ctx);

/* Pump the network once (drains hardware RX into the ring, services ARP/ICMP).
 * Call repeatedly while wolfSSL returns WANT_READ/WANT_WRITE. In a DTLS_RX_IRQ
 * build RX is serviced in the ethmac ISR, so this is a no-op. */
void dtls_io_pump(void);

#ifdef DTLS_RX_IRQ
/* Enable interrupt-driven RX (attach ethmac ISR, enable the RX event, unmask the
 * CPU line). Call after ARP resolution and before the handshake. */
void dtls_io_enable_irq(void);
#endif

/* Elapsed seconds since dtls_io_init(), backed by LiteX timer0. Also used by
 * wolfSSL as LowResTimer(). */
uint32_t dtls_uptime_seconds(void);

/* Millisecond-resolution monotonic sim-time (same 1 MHz tick source). Stable
 * basis for comparing handshake latency across builds. */
uint32_t dtls_uptime_millis(void);

#endif /* DTLS_IO_H */
