#!/usr/bin/env bash
# Regenerate the self-signed ECC P-256 server cert/key and the firmware's embedded
# DER header. Run from anywhere; paths below are relative to the repo root.
#
# Private key is gitignored (host/certs/*-key.pem) and never touched by git;
# the public cert/DER and boot/certs.h are tracked and must be regenerated
# together whenever the key changes.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
certs_dir="$repo_root/host/certs"
key="$certs_dir/server-key.pem"
cert_pem="$certs_dir/server-cert.pem"
cert_der="$certs_dir/server-cert.der"
certs_h="$repo_root/boot/certs.h"

mkdir -p "$certs_dir"

openssl ecparam -name prime256v1 -genkey -noout -out "$key"

openssl req -new -x509 -key "$key" -out "$cert_pem" -days 3650 \
  -subj "/O=PQC-DTLS-Demo/CN=192.168.1.100" \
  -addext "subjectAltName=IP:192.168.1.100"

openssl x509 -in "$cert_pem" -outform DER -out "$cert_der"

{
  echo "/* Auto-generated from host/certs/server-cert.der (self-signed ECC P-256, CN=192.168.1.100)."
  echo " * Embedded into the firmware as the trusted CA so the DTLS client can authenticate"
  echo " * the server. Regenerate: scripts/gen_certs.sh */"
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
