/*
 * dtls_io.c - wolfSSL <-> liteeth UDP transport glue. See dtls_io.h.
 */
#include <stdio.h>
#include <string.h>
#include <sys/types.h>              /* ssize_t, used by wolfio.h's DTLS typedefs */
#include <generated/csr.h>
#include <generated/soc.h>          /* CONFIG_CLOCK_FREQUENCY */
#include <libliteeth/udp.h>

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/ssl.h>
#include <wolfssl/wolfio.h>          /* WOLFSSL_CBIO_ERR_*, CallbackIO* */

#include "dtls_io.h"

/* ------------------------------------------------------------------ RX ring */
/* Whole-datagram ring. The liteeth callback hands us a pointer into the HW RX
 * slot that is only valid for the duration of the callback, so we copy each
 * datagram out immediately. Single producer (udp_service, in the main loop) and
 * single consumer (recv callback, also main loop) => no locking needed. */
#define DTLS_RX_SLOTS   8
#define DTLS_MAX_DGRAM  1536

static uint8_t  rx_buf[DTLS_RX_SLOTS][DTLS_MAX_DGRAM];
static uint16_t rx_len[DTLS_RX_SLOTS];
static int      rx_head;   /* next write slot */
static int      rx_tail;   /* next read slot  */

static uint16_t g_local_port;
static uint16_t g_peer_port;

/* liteeth RX callback: copy the datagram into the ring (drop if full). */
static void dtls_rx_cb(uint32_t src_ip, uint16_t src_port, uint16_t dst_port,
                       void *data, uint32_t length)
{
    (void)src_ip; (void)src_port; (void)dst_port;
    int next = (rx_head + 1) % DTLS_RX_SLOTS;
    if (next == rx_tail)
        return;                      /* ring full: drop (DTLS will retransmit) */
    uint32_t n = length;
    if (n > DTLS_MAX_DGRAM)
        n = DTLS_MAX_DGRAM;
    memcpy(rx_buf[rx_head], data, n);
    rx_len[rx_head] = (uint16_t)n;
    rx_head = next;
}

/* wolfSSL recv: return exactly one datagram, or WANT_READ if none pending. */
static int dtls_recv_cb(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
    (void)ssl; (void)ctx;
    if (rx_head == rx_tail)
        return WOLFSSL_CBIO_ERR_WANT_READ;
    uint32_t n = rx_len[rx_tail];
    if ((int)n > sz)
        n = (uint32_t)sz;            /* should not happen with a sane MTU */
    memcpy(buf, rx_buf[rx_tail], n);
    rx_tail = (rx_tail + 1) % DTLS_RX_SLOTS;
    printf("[io] RX %d bytes -> wolfSSL\n", (int)n);
    return (int)n;
}

/* wolfSSL send: one record -> one UDP datagram to the peer. */
static int dtls_send_cb(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
    (void)ssl; (void)ctx;
    if (sz <= 0)
        return WOLFSSL_CBIO_ERR_GENERAL;
    if (sz > DTLS_MAX_DGRAM)
        return WOLFSSL_CBIO_ERR_GENERAL;   /* keep MTU small enough to avoid */
    printf("[io] TX %d bytes ...\n", sz);
    void *tx = udp_get_tx_buffer();
    memcpy(tx, buf, sz);
    if (!udp_send(g_local_port, g_peer_port, (uint32_t)sz)) {
        printf("[io] TX blocked (ARP not ready)\n");
        return WOLFSSL_CBIO_ERR_WANT_WRITE; /* ARP not ready yet */
    }
    printf("[io] TX %d bytes done\n", sz);
    return sz;
}

/* -------------------------------------------------------------- free timer */
/* LiteX timer0 as a free-running down-counter for a monotonic microsecond-ish
 * clock. reload/load = 0xFFFFFFFF, so it counts down and wraps; we accumulate
 * wraps to build a 64-bit tick count. Nothing else in this firmware uses
 * timer0 (the sim Ethernet PHY needs no busy_wait reset). */
static uint32_t timer_last_val;
static uint64_t timer_wrap_acc;

static void dtls_timer_init(void)
{
    timer0_en_write(0);
    timer0_reload_write(0xFFFFFFFFu);
    timer0_load_write(0xFFFFFFFFu);
    timer0_en_write(1);
    timer0_update_value_write(1);
    timer_last_val = timer0_value_read();
    timer_wrap_acc = 0;
}

static uint64_t dtls_timer_ticks(void)
{
    timer0_update_value_write(1);
    uint32_t v = timer0_value_read();
    if (v > timer_last_val)                 /* counter reloaded => wrapped */
        timer_wrap_acc += 0x100000000ULL;
    timer_last_val = v;
    return timer_wrap_acc + (uint64_t)(0xFFFFFFFFu - v);
}

uint32_t dtls_uptime_seconds(void)
{
    return (uint32_t)(dtls_timer_ticks() / (uint64_t)CONFIG_CLOCK_FREQUENCY);
}

/* wolfSSL needs this because NO_ASN_TIME suppresses its built-in LowResTimer. */
word32 LowResTimer(void)
{
    return (word32)dtls_uptime_seconds();
}

/* ------------------------------------------------------------------- setup */
void dtls_io_init(uint16_t local_port, uint16_t peer_port)
{
    g_local_port = local_port;
    g_peer_port  = peer_port;
    rx_head = rx_tail = 0;
    dtls_timer_init();
    udp_set_callback(dtls_rx_cb);
}

void dtls_io_register(WOLFSSL_CTX *ctx)
{
    wolfSSL_SetIORecv(ctx, dtls_recv_cb);
    wolfSSL_SetIOSend(ctx, dtls_send_cb);
}

void dtls_io_pump(void)
{
    udp_service();
}
