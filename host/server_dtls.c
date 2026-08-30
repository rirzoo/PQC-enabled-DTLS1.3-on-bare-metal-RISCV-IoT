/*
 * Minimal PQC-DTLS 1.3 server (host side) for the RISC-V firmware demo.
 *
 *   - DTLS 1.3 over UDP, bound to 192.168.1.100:11111
 *   - ML-KEM-512 post-quantum key exchange (client selects the group)
 *   - Server authenticated with an ECC P-256 certificate
 *   - Echoes one application-data message back to the client
 *
 * Build (against the in-tree wolfSSL built with --enable-dtls13
 * --enable-dtls-frag-ch --enable-mlkem --enable-experimental):
 *   gcc server_dtls.c -I wolfssl -L wolfssl/src/.libs -lwolfssl -o server_dtls
 *   LD_LIBRARY_PATH=wolfssl/src/.libs ./server_dtls
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>
#include <wolfssl/wolfio.h>          /* WOLFSSL_CBIO_ERR_*, wolfSSL_CTX_SetIOSend */

#define LISTEN_IP    "192.168.1.100"
#define LISTEN_PORT  11111
#define CERT_FILE    "certs/server-cert.pem"
#define KEY_FILE     "certs/server-key.pem"

/*
 * Paced DTLS send. The RISC-V client runs in a ~1 MHz Verilator sim and its
 * liteeth MAC has only a couple of hardware RX slots. wolfSSL flushes an entire
 * handshake flight (ServerHello, EncryptedExtensions, Certificate, CertVerify,
 * Finished) as a burst of datagrams within a fraction of a millisecond; the slow
 * client, busy in ML-KEM/cert crypto, cannot drain the MAC in time, so the tail
 * of each burst (notably the 66-byte Finished record) is dropped in hardware and
 * the handshake never completes. Spacing the datagrams gives the client time to
 * drain each one. Delay is tunable via DTLS_PACE_US (microseconds) so it can be
 * adjusted without recompiling. See PROGRESS.md B6.
 */
static long g_pace_us = 25000;   /* default 25 ms between flight datagrams */

static int PacedSend(WOLFSSL *ssl, char *buf, int sz, void *ctx)
{
    /* Delegate to wolfSSL's own DTLS sender: for DTLS the write context is a
     * WOLFSSL_DTLS_CTX struct (peer sockaddr + rfd/wfd), NOT a bare int fd, so
     * we must not touch it ourselves. EmbedSendTo does the sendto() correctly;
     * we only add the inter-datagram delay afterwards. */
    int sent = EmbedSendTo(ssl, buf, sz, ctx);
    if (sent > 0 && g_pace_us > 0) {
        struct timespec ts = { g_pace_us / 1000000L,
                               (g_pace_us % 1000000L) * 1000L };
        nanosleep(&ts, NULL);
    }
    return sent;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* unbuffered: logs survive kill */

    const char *pace_env = getenv("DTLS_PACE_US");
    if (pace_env)
        g_pace_us = strtol(pace_env, NULL, 10);

    const char *listen_ip = (argc > 1) ? argv[1] : LISTEN_IP;

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) { perror("socket"); return 1; }

    int on = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = inet_addr(listen_ip);
    addr.sin_port        = htons(LISTEN_PORT);
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    printf("PQC-DTLS 1.3 server listening on %s:%d\n", listen_ip, LISTEN_PORT);

    wolfSSL_Init();
    /* wolfSSL_Debugging_ON(); */

    WOLFSSL_CTX *ctx = wolfSSL_CTX_new(wolfDTLSv1_3_server_method());
    if (!ctx) { fprintf(stderr, "CTX_new failed\n"); return 1; }

    /* Pace handshake-flight datagrams so the slow sim client can keep up. */
    wolfSSL_CTX_SetIOSend(ctx, PacedSend);
    printf("(flight pacing: %ld us between datagrams)\n", g_pace_us);

    if (wolfSSL_CTX_use_certificate_file(ctx, CERT_FILE, WOLFSSL_FILETYPE_PEM)
            != WOLFSSL_SUCCESS) {
        fprintf(stderr, "load cert %s failed\n", CERT_FILE); return 1;
    }
    if (wolfSSL_CTX_use_PrivateKey_file(ctx, KEY_FILE, WOLFSSL_FILETYPE_PEM)
            != WOLFSSL_SUCCESS) {
        fprintf(stderr, "load key %s failed\n", KEY_FILE); return 1;
    }

    for (;;) {
        /* Peek the first datagram to learn the client, then connect the socket
         * to it so wolfSSL's default IO talks only to this peer. */
        struct sockaddr_in peer;
        socklen_t peerlen = sizeof(peer);
        unsigned char b[1];
        if (recvfrom(sockfd, b, sizeof(b), MSG_PEEK,
                     (struct sockaddr *)&peer, &peerlen) < 0) {
            perror("recvfrom(peek)"); continue;
        }
        printf("Client hello from %s:%d\n",
               inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));

        if (connect(sockfd, (struct sockaddr *)&peer, peerlen) != 0) {
            perror("connect"); continue;
        }

        WOLFSSL *ssl = wolfSSL_new(ctx);
        if (!ssl) { fprintf(stderr, "SSL_new failed\n"); break; }
        wolfSSL_set_fd(ssl, sockfd);

        if (wolfSSL_accept(ssl) != WOLFSSL_SUCCESS) {
            int err = wolfSSL_get_error(ssl, 0);
            char es[80];
            fprintf(stderr, "accept failed: err %d (%s)\n",
                    err, wolfSSL_ERR_error_string(err, es));
        } else {
            printf("*** DTLS 1.3 handshake complete ***\n");
            printf("  version: %s\n", wolfSSL_get_version(ssl));
            printf("  cipher : %s\n", wolfSSL_get_cipher(ssl));
            printf("  group  : %s\n", wolfSSL_get_curve_name(ssl));

            char buf[256];
            int n = wolfSSL_read(ssl, buf, sizeof(buf) - 1);
            if (n > 0) {
                buf[n] = '\0';
                printf("  client says: %s\n", buf);
            }
            const char *reply = "Hello from PQC-DTLS 1.3 Linux server!";
            wolfSSL_write(ssl, reply, (int)strlen(reply));
            printf("  replied; session done\n");
        }

        wolfSSL_shutdown(ssl);
        wolfSSL_free(ssl);

        /* Un-connect the socket so we can serve the next client. */
        struct sockaddr_in unspec;
        memset(&unspec, 0, sizeof(unspec));
        unspec.sin_family = AF_UNSPEC;
        connect(sockfd, (struct sockaddr *)&unspec, sizeof(unspec));
    }

    wolfSSL_CTX_free(ctx);
    wolfSSL_Cleanup();
    close(sockfd);
    return 0;
}
