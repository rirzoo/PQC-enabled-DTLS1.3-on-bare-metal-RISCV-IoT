/*
 * PQC-DTLS 1.3 client firmware for a bare-metal LiteX/VexRiscv RISC-V SoC.
 *
 * Transport: liteeth UDP (see dtls_io.c for the wolfSSL<->liteeth glue).
 * Security:  DTLS 1.3, ML-KEM-512 key exchange (PQC KEM), server authenticated
 *            with an embedded ECC (P-256) CA certificate.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <generated/csr.h>
#include <irq.h>
#include <libliteeth/udp.h>
#include <libliteeth/inet.h>
#include <libbase/console.h>
#include <libbase/uart.h>

#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/ssl.h>

#include "dtls_io.h"
#include "certs.h"          /* server_cert_der[] - trusted CA (server's self-signed cert) */

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

/* -------- network identity (matches litex_sim --local-ip/--remote-ip) -------- */
#define MY_IP        IPTOINT(192, 168, 1, 50)
#define SERVER_IP    IPTOINT(192, 168, 1, 100)
#define MY_MAC       {0x02, 0x00, 0x00, 0x00, 0x00, 0x01}
#define CLIENT_PORT  5555
#define SERVER_PORT  11111

/* Give up on the handshake after this many simulated seconds (safety net). Raised to
 * 120 s for ML-DSA: the larger Certificate/CertificateVerify flight plus ML-DSA verify
 * needs headroom so a slow-but-working config is not false-FAILed by the timer. A run
 * that still cannot complete within 120 s is a genuine failure. */
#define HANDSHAKE_TIMEOUT_S  120

/*
 * Entropy source (CUSTOM_RAND_GENERATE_SEED). Mixes the free-running timer with
 * a small LCG. NOTE: this is NOT a cryptographically secure TRNG - it is a
 * placeholder for a real hardware entropy source (a later deliverable).
 */
int CustomRngGenerateBlock(unsigned char *output, unsigned int sz)
{
    static uint32_t s = 0x2545F491u;
    for (unsigned int i = 0; i < sz; i++) {
        timer0_update_value_write(1);
        s ^= timer0_value_read();
        s = s * 1664525u + 1013904223u;   /* LCG mix */
        output[i] = (unsigned char)(s >> 24);
    }
    return 0;
}

int main(void)
{
    uint8_t mac[] = MY_MAC;

#ifdef CONFIG_CPU_HAS_INTERRUPT
    irq_setmask(0);
    irq_setie(1);
#endif
    uart_init();
    printf("\n=== PQC-DTLS 1.3 RISC-V client ===\n");

    /* Bring up the UDP stack and resolve the server's MAC via ARP. */
    udp_start(mac, MY_IP);
    printf("Resolving server (192.168.1.100) via ARP...\n");
    while (udp_arp_resolve(SERVER_IP) == 0)
        udp_service();
    printf("Server MAC resolved.\n");

    /* Wire wolfSSL to the liteeth transport. */
    dtls_io_init(CLIENT_PORT, SERVER_PORT);

#ifdef DTLS_RX_IRQ
    /* Switch RX from polling to interrupt-driven now that the RX callback is
     * installed and ARP is resolved. The ethmac ISR drains the MAC into the ring
     * even while the CPU is mid-crypto, so the server's handshake-flight burst is
     * not dropped at the 2 HW RX slots (removes the need for server-side pacing). */
    dtls_io_enable_irq();
    printf("Interrupt-driven RX enabled (ethmac irq %d).\n", ETHMAC_INTERRUPT);
#endif

    wolfSSL_Init();
#ifdef DEBUG_WOLFSSL
    wolfSSL_Debugging_ON();
#endif

    WOLFSSL_CTX *ctx = wolfSSL_CTX_new(wolfDTLSv1_3_client_method());
    if (ctx == NULL) {
        printf("FATAL: wolfSSL_CTX_new failed\n");
        return -1;
    }
    dtls_io_register(ctx);

    /* Trust the server's certificate (embedded DER CA). */
    if (wolfSSL_CTX_load_verify_buffer(ctx, server_cert_der, server_cert_der_len,
                                       WOLFSSL_FILETYPE_ASN1) != WOLFSSL_SUCCESS) {
        printf("FATAL: load_verify_buffer failed\n");
        return -1;
    }

    WOLFSSL *ssl = wolfSSL_new(ctx);
    if (ssl == NULL) {
        printf("FATAL: wolfSSL_new failed\n");
        return -1;
    }

    wolfSSL_dtls_set_using_nonblock(ssl, 1);
    wolfSSL_dtls_set_mtu(ssl, 1400);

    /* Post-quantum key exchange: ML-KEM-512. */
    if (wolfSSL_UseKeyShare(ssl, WOLFSSL_ML_KEM_512) != WOLFSSL_SUCCESS) {
        printf("FATAL: UseKeyShare(ML_KEM_512) failed\n");
        return -1;
    }

    printf("Starting DTLS 1.3 handshake (ML-KEM-512)...\n");
    uint32_t t_start = dtls_uptime_seconds();
    uint32_t t_start_ms = dtls_uptime_millis();
    uint32_t rtx_mark = t_start;
    int last_err = 0;
    unsigned long pumps = 0;
    int ret;
    for (;;) {
        ret = wolfSSL_connect(ssl);
        if (ret == WOLFSSL_SUCCESS)
            break;

        int err = wolfSSL_get_error(ssl, ret);
        if (err != last_err) {
            printf("[hs] connect err %d (t=%us)\n", err,
                   (unsigned)(dtls_uptime_seconds() - t_start));
            last_err = err;
        }
        if (err == WOLFSSL_ERROR_WANT_READ || err == WOLFSSL_ERROR_WANT_WRITE) {
            dtls_io_pump();
            if ((++pumps % 20000UL) == 0)
                printf("[hs] alive, pumps=%lu t=%us\n", pumps,
                       (unsigned)(dtls_uptime_seconds() - t_start));
            uint32_t now = dtls_uptime_seconds();
            int to = wolfSSL_dtls_get_current_timeout(ssl);
            if (to > 0 && (int)(now - rtx_mark) >= to) {
                wolfSSL_dtls_got_timeout(ssl);   /* retransmit last flight */
                rtx_mark = now;
            }
            if ((now - t_start) >= HANDSHAKE_TIMEOUT_S) {
                printf("FAIL: handshake timed out after %u s\n",
                       (unsigned)(now - t_start));
                goto cleanup;
            }
            continue;
        }
        printf("FAIL: wolfSSL_connect error %d\n", err);
        goto cleanup;
    }

    printf("\n*** DTLS 1.3 HANDSHAKE COMPLETE ***\n");
    printf("  handshake time: %u ms (sim-time; incl. transport + server pacing)\n",
           (unsigned)(dtls_uptime_millis() - t_start_ms));
    printf("  version : %s\n", wolfSSL_get_version(ssl));
    printf("  cipher  : %s\n", wolfSSL_get_cipher(ssl));
    {
        const char *grp = wolfSSL_get_curve_name(ssl);
        printf("  group   : %s\n", grp ? grp : "(n/a)");
    }

    /* Exchange one application-data message over the secure channel. */
    {
        const char *msg = "Hello from RISC-V DTLS 1.3 client!";
        int n = wolfSSL_write(ssl, msg, (int)strlen(msg));
        printf("  wrote %d app bytes\n", n);

        char in[128];
        int got;
        uint32_t rd_start = dtls_uptime_seconds();
        do {
            got = wolfSSL_read(ssl, in, (int)sizeof(in) - 1);
            if (got > 0) {
                in[got] = '\0';
                printf("  server says: %s\n", in);
                break;
            }
            int err = wolfSSL_get_error(ssl, got);
            if (err == WOLFSSL_ERROR_WANT_READ) {
                dtls_io_pump();
            } else {
                printf("  read error %d\n", err);
                break;
            }
        } while ((dtls_uptime_seconds() - rd_start) < 10);
    }

    printf("=== session done ===\n");

cleanup:
    wolfSSL_free(ssl);
    wolfSSL_CTX_free(ctx);
    wolfSSL_Cleanup();

    while (1)
        udp_service();
    return 0;
}
