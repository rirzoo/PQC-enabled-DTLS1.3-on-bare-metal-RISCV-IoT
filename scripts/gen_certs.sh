#!/usr/bin/env bash
# Regenerate the self-signed ML-DSA-44 (Dilithium2) server cert/key and the
# firmware's embedded DER trust anchor (boot/certs.h).
#
# OpenSSL cannot mint ML-DSA certs here (3.2, no OQS provider), so this compiles
# and runs scripts/gen_mldsa_cert.c against host/wolfssl (which must be built with
# --enable-dilithium --enable-certgen --enable-keygen).
#
# The private key is gitignored (host/certs/*-key.pem) and never touched by git;
# the public cert/DER and boot/certs.h are tracked and must be regenerated
# together whenever the key changes.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
certs_dir="$repo_root/host/certs"
wolfssl_dir="$repo_root/host/wolfssl"
libdir="$wolfssl_dir/src/.libs"
tool_src="$repo_root/scripts/gen_mldsa_cert.c"
tool_bin="$repo_root/scripts/gen_mldsa_cert"
key="$certs_dir/server-key.pem"
cert_pem="$certs_dir/server-cert.pem"
cert_der="$certs_dir/server-cert.der"
certs_h="$repo_root/boot/certs.h"

mkdir -p "$certs_dir"

if [ ! -f "$libdir/libwolfssl.so" ]; then
  echo "ERROR: $libdir/libwolfssl.so not found." >&2
  echo "Build host wolfSSL first (--enable-dilithium --enable-certgen --enable-keygen)." >&2
  exit 1
fi

echo "Compiling ML-DSA cert tool..."
gcc -O2 -Wall -o "$tool_bin" "$tool_src" \
  -I"$wolfssl_dir" -L"$libdir" -lwolfssl -Wl,-rpath,"$libdir"

echo "Generating ML-DSA-44 cert/key..."
( cd "$repo_root" && "$tool_bin" "$certs_dir" )

{
  echo "/* Auto-generated from host/certs/server-cert.der (self-signed ML-DSA-44"
  echo " * / Dilithium2, CN=192.168.1.100). Embedded into the firmware as the trusted"
  echo " * CA so the DTLS client can post-quantum-authenticate the server."
  echo " * Regenerate: scripts/gen_certs.sh */"
  echo "#ifndef CERTS_H"
  echo "#define CERTS_H"
  echo
  xxd -i -n server_cert_der "$cert_der"
  echo
  echo "#endif /* CERTS_H */"
} > "$certs_h"

echo "Regenerated:"
echo "  $key (private, gitignored)"
echo "  $cert_pem"
echo "  $cert_der"
echo "  $certs_h"
