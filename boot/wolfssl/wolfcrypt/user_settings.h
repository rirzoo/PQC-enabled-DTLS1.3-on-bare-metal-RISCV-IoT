#ifndef USER_SETTINGS_H
#define USER_SETTINGS_H

/* Provide ssize_t before any wolfSSL header: with WOLFSSL_DTLS enabled, wolfio.h
 * declares recv/send-from typedefs in terms of ssize_t, which the freestanding
 * (picolibc) toolchain does not define implicitly. */
#include <sys/types.h>

// #define WOLFCRYPT_ONLY // only use the crypto backend

#define WOLFSSL_SP_MATH // maths backend for crypto

#define NO_TIME_H
#define WOLFSSL_NO_CLOCK
#define NO_ASN_TIME

/* user_settings.h */
#define WOLFSSL_NO_SOCK
#define NO_WRITEV
#define WOLFSSL_USER_IO
#define WOLFSSL_USE_ALIGN           // rv32 VexRiscv 'standard' has no HW unaligned
                                    // access; force byte-wise c32toa/ato32 etc.
                                    // (paired with -mstrict-align in the Makefile)
#define WOLFSSL_SMALL_STACK         // Optimize for small stack usage
#define WOLFSSL_SMALL_CERT_VERIFY   // Lower memory certificate verification
#define NO_FILESYSTEM               // Don't use file system
#define NO_WOLFSSL_DIR              // Don't use directory access
// #define NO_WOLFSSL_SERVER           // Client only, no server support
#define SINGLE_THREADED             // No threading support needed
#define NO_ERROR_STRINGS            // Save space by removing error strings
#define WOLFSSL_SMALL_SESSION_CACHE // Smaller session cache
// #define NO_OLD_TLS                  // Only support TLS 1.2+
#define WOLFSSL_TLS13
#define HAVE_TLS_EXTENSIONS

// ---- DTLS 1.3 (RFC 9147) ----
#define WOLFSSL_DTLS                // DTLS base support
#define WOLFSSL_DTLS13              // DTLS 1.3
#define WOLFSSL_SEND_HRR_COOKIE    // required by wolfSSL's dtls.c when DTLS 1.3 is
                                   // compiled (server HRR-cookie code lives in the
                                   // same file); harmless for our client role
#define HAVE_SUPPORTED_CURVES      // TLS "supported groups" extension (required to
                                   // negotiate the ML-KEM key-share group)
#define WOLFSSL_DTLS_MTU           // enable wolfSSL_dtls_set_mtu() to bound record
                                   // size to one Ethernet frame
#define WOLFSSL_DTLS_CH_FRAG       // fragment the (large, PQC) ClientHello across
                                   // datagrams — REQUIRED for DTLS 1.3 + ML-KEM, else
                                   // the ClientHello exceeds one datagram and the
                                   // handshake fails (must match server's
                                   // --enable-dtls-frag-ch)
// Firmware is the DTLS client; it must supply LowResTimer() (seconds) because
// NO_ASN_TIME disables wolfSSL's built-in timer. Provided in boot/dtls_io.c.

#define WC_RSA_PSS
#define NO_DH                       // Disable DH to save space

#define HAVE_X25519
#define HAVE_CURVE25519
#define WOLFSSL_SHA256
#define WOLFSSL_SHA512
#define HAVE_ECC             // Enable ECC
#define HAVE_ED25519         // Enable ED25519
#define HAVE_RSA             // Enable RSA
#define HAVE_AESGCM          // Enable AES-GCM
#define WC_RSA_BLINDING      // Enable RSA blinding
#define ECC_TIMING_RESISTANT // Make ECC resistant to timing attacks

#define WOLFSSL_WC_MLKEM
#define WOLFSSL_HAVE_MLKEM
#define WOLFSSL_HAVE_KYBER // enable ML-KEM in wolfSSL
#define WOLFSSL_SHA3       // ML-KEM uses SHA3/SHAKE
#define WOLFSSL_SHAKE256
#define WOLFSSL_SHAKE128
#define WOLFSSL_MLKEM_ENCAPSULATE_SMALL_MEM
#define WOLFSSL_MLKEM_MAKEKEY_SMALL_MEM

/* ML-DSA (Dilithium) — PQ server-cert authentication (client verifies only).
 * Pairs with ML-KEM-512; the server presents an ML-DSA-44 cert. SHA3/SHAKE is
 * already enabled above for ML-KEM (ML-DSA's hash dependency). */
#define HAVE_DILITHIUM
#define WOLFSSL_WC_DILITHIUM
#define WOLFSSL_DILITHIUM_VERIFY_ONLY      // client never signs -> drop keygen+sign code
#define WOLFSSL_DILITHIUM_VERIFY_SMALL_MEM // small-stack friendly (WOLFSSL_SMALL_STACK on)

#define HAVE_HKDF
#define WOLFSSL_KEY_GEN
#define WOLFSSL_HAVE_SP_RSA

#define WOLFSSL_HAVE_SP_ECC
#define HAVE_ECC256 // specifically enable secp256r1

// debug support - OFF by default: on the 1 MHz sim every log line is a slow UART
// write, which dominates handshake time. Re-enable a single line to debug.
// #define DEBUG_WOLFSSL
// #define SHOW_GEN
// #define DEBUG_WOLFSSL_VERBOSE

extern int CustomRngGenerateBlock(unsigned char *, unsigned int);
#define CUSTOM_RAND_GENERATE_SEED CustomRngGenerateBlock

#endif // USER_SETTINGS_H