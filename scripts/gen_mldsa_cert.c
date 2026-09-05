/*
 * gen_mldsa_cert.c - generate a self-signed ML-DSA-44 (Dilithium2) server
 * certificate + private key for the DTLS 1.3 server.
 *
 * OpenSSL 3.2 cannot mint ML-DSA certs (needs 3.5+/an OQS provider), so cert
 * generation uses wolfCrypt from host/wolfssl, which must be built with
 * --enable-dilithium --enable-certgen --enable-keygen. Driven by
 * scripts/gen_certs.sh (which also regenerates boot/certs.h from the DER).
 *
 * Usage: gen_mldsa_cert <certs_dir>
 *   writes <certs_dir>/server-key.pem   (PKCS#8 ML-DSA-44 private key)
 *          <certs_dir>/server-cert.pem  (self-signed cert, CN=192.168.1.100)
 *          <certs_dir>/server-cert.der  (same cert, DER; embedded in firmware)
 */
#include <wolfssl/options.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/dilithium.h>
#include <wolfssl/wolfcrypt/asn_public.h>
#include <wolfssl/wolfcrypt/random.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CN          "192.168.1.100"
#define ORG         "PQC-DTLS-Demo"
#define VALID_DAYS  3650

/* ML-DSA-44 cert (~1.3 KB pubkey + ~2.4 KB sig) fits comfortably in 8 KiB. */
#define DER_BUF_SZ  8192
#define PEM_BUF_SZ  16384

static int write_bytes(const char *path, const unsigned char *buf, int len)
{
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "open %s failed\n", path); return -1; }
    if (fwrite(buf, 1, (size_t)len, f) != (size_t)len) {
        fprintf(stderr, "write %s failed\n", path); fclose(f); return -1;
    }
    fclose(f);
    printf("  wrote %s (%d bytes)\n", path, len);
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "host/certs";
    char path[512];
    dilithium_key key;
    WC_RNG rng;
    Cert cert;
    unsigned char der[DER_BUF_SZ];
    unsigned char pem[PEM_BUF_SZ];
    int ret, derSz, pemSz;
    int rc = 1;

    if ((ret = wc_InitRng(&rng)) != 0) {
        fprintf(stderr, "wc_InitRng: %d\n", ret); return 1;
    }
    if ((ret = wc_dilithium_init(&key)) != 0) {
        fprintf(stderr, "wc_dilithium_init: %d\n", ret); goto out_rng;
    }
    if ((ret = wc_dilithium_set_level(&key, 2)) != 0) {   /* ML-DSA-44 */
        fprintf(stderr, "wc_dilithium_set_level: %d\n", ret); goto out_key;
    }
    printf("Generating ML-DSA-44 (Dilithium2) key pair...\n");
    if ((ret = wc_dilithium_make_key(&key, &rng)) != 0) {
        fprintf(stderr, "wc_dilithium_make_key: %d\n", ret); goto out_key;
    }

    /* ---- private key -> PKCS#8 DER -> PEM (BEGIN PRIVATE KEY) ---- */
    derSz = wc_Dilithium_PrivateKeyToDer(&key, der, sizeof(der));
    if (derSz < 0) {
        fprintf(stderr, "wc_Dilithium_PrivateKeyToDer: %d\n", derSz); goto out_key;
    }
    pemSz = wc_DerToPem(der, (word32)derSz, pem, sizeof(pem), PKCS8_PRIVATEKEY_TYPE);
    if (pemSz < 0) {
        fprintf(stderr, "wc_DerToPem(key): %d\n", pemSz); goto out_key;
    }
    snprintf(path, sizeof(path), "%s/server-key.pem", dir);
    if (write_bytes(path, pem, pemSz) != 0) goto out_key;

    /* ---- self-signed cert, signed with the ML-DSA key ---- */
    if ((ret = wc_InitCert(&cert)) != 0) {
        fprintf(stderr, "wc_InitCert: %d\n", ret); goto out_key;
    }
    strncpy(cert.subject.country,  "US",  CTC_NAME_SIZE - 1);
    strncpy(cert.subject.org,      ORG,   CTC_NAME_SIZE - 1);
    strncpy(cert.subject.commonName, CN,  CTC_NAME_SIZE - 1);
    cert.sigType    = CTC_ML_DSA_LEVEL2;   /* sign with ML-DSA-44 */
    cert.daysValid  = VALID_DAYS;
    cert.selfSigned = 1;
    cert.isCA       = 1;                    /* trust anchor embedded in firmware */

    printf("Building self-signed ML-DSA-44 cert (CN=%s)...\n", CN);
    derSz = wc_MakeCert_ex(&cert, der, sizeof(der), ML_DSA_LEVEL2_TYPE, &key, &rng);
    if (derSz < 0) {
        fprintf(stderr, "wc_MakeCert_ex: %d\n", derSz); goto out_key;
    }
    derSz = wc_SignCert_ex(cert.bodySz, cert.sigType, der, sizeof(der),
                           ML_DSA_LEVEL2_TYPE, &key, &rng);
    if (derSz < 0) {
        fprintf(stderr, "wc_SignCert_ex: %d\n", derSz); goto out_key;
    }

    snprintf(path, sizeof(path), "%s/server-cert.der", dir);
    if (write_bytes(path, der, derSz) != 0) goto out_key;

    pemSz = wc_DerToPem(der, (word32)derSz, pem, sizeof(pem), CERT_TYPE);
    if (pemSz < 0) {
        fprintf(stderr, "wc_DerToPem(cert): %d\n", pemSz); goto out_key;
    }
    snprintf(path, sizeof(path), "%s/server-cert.pem", dir);
    if (write_bytes(path, pem, pemSz) != 0) goto out_key;

    printf("ML-DSA-44 cert/key generation complete.\n");
    rc = 0;

out_key:
    wc_dilithium_free(&key);
out_rng:
    wc_FreeRng(&rng);
    return rc;
}
