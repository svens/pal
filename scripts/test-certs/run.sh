#!/bin/sh
# Generate test certificate hierarchy for pal/crypto tests.
#
# Produces:
#   pal/crypto/test.hpp         -- cert PEMs and expected property values
#   pal/crypto/test_pkcs12.hpp  -- PKCS#12 binary as constexpr uint8_t array
#
# Run from the repository root:
#   sh scripts/test-certs/run.sh
#
# Requires: openssl CLI
#
# To regenerate test certificates, run this script from the repository root,
# then run 'ninja smoke'. Failing tests will print the actual subject_name,
# issuer_name, and *_alternative_name values -- copy them into test_certs.hpp.
#
# PKCS#12 cipher: PBE-SHA1-3DES for compatibility with WinCrypt on Windows.

set -e

REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

# ----- OpenSSL configuration --------------------------------------------------

cat > openssl.conf << 'OPENSSL_CONF'
[ ca ]
default_ca = CA_default

[ CA_default ]
dir = .
new_certs_dir = ./newcerts
database = ./index.txt
serial = ./serial
default_md = sha256
name_opt = ca_default
cert_opt = ca_default
preserve = no
policy = policy_match
copy_extensions = copy
unique_subject = no

[ policy_match ]
C = match
ST = match
O = match
OU = optional
CN = supplied

[ req ]
default_bits = 2048
default_md = sha256
distinguished_name = req_distinguished_name
string_mask = utf8only
x509_extensions = v3_ca

[ req_distinguished_name ]
C = Country Name
ST = State
O = Organization
OU = Organizational Unit
CN = Common Name
emailAddress = Email Address

[ v3_ca ]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints = critical, CA:true
keyUsage = critical, digitalSignature, keyCertSign, cRLSign

[ v3_intermediate_ca ]
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints = critical, CA:true, pathlen:0
keyUsage = critical, digitalSignature, keyCertSign, cRLSign

[ server_cert ]
basicConstraints = CA:FALSE
nsCertType = server
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer:always
issuerAltName = DNS:ca.pal.alt.ee,URI:https://ca.pal.alt.ee/path
subjectAltName = @server_alt_names
keyUsage = critical, digitalSignature, keyEncipherment
extendedKeyUsage = serverAuth

[ server_alt_names ]
IP.1 = 1.2.3.4
IP.2 = 2001:db8:85a3::8a2e:370:7334
DNS.1 = *.pal.alt.ee
DNS.2 = server.pal.alt.ee
email.1 = pal@alt.ee
URI.1 = https://pal.alt.ee/path

[ client_cert ]
basicConstraints = CA:FALSE
nsCertType = client, email
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer
keyUsage = critical, nonRepudiation, digitalSignature, keyEncipherment
extendedKeyUsage = clientAuth, emailProtection
subjectAltName = @client_alt_names

[ client_alt_names ]
email.1 = pal@alt.ee
DNS.1 = client.pal.alt.ee

[ v3_self_signed ]
# No subjectKeyIdentifier or authorityKeyIdentifier -- tests parser with
# optional extensions absent. Far-future expiry forces GeneralizedTime
# date format (4-digit year), exercising a different parsing code path.
keyUsage = critical, digitalSignature, keyEncipherment
extendedKeyUsage = clientAuth, serverAuth
OPENSSL_CONF

mkdir newcerts
touch index.txt
echo 1000 > serial

# ----- Key and certificate generation -----------------------------------------

# All keys are 2048-bit RSA, unencrypted (test-only).

make_self_signed() # name subject extensions [days=7300]
{
	echo "==> $1"
	openssl genrsa -out "$1.key.pem" 2048 2>/dev/null
	openssl req -new -x509 -batch -config openssl.conf \
		-key "$1.key.pem" \
		-out "$1.pem" \
		-subj "$2" \
		-days "${4:-7300}" \
		-extensions "$3"
}

make_ec_self_signed() # name subject extensions [days=7300]
{
	echo "==> $1"
	openssl ecparam -name prime256v1 -genkey -noout -out "$1.key.pem" 2>/dev/null
	openssl req -new -x509 -batch -config openssl.conf \
		-key "$1.key.pem" \
		-out "$1.pem" \
		-subj "$2" \
		-days "${4:-7300}" \
		-extensions "$3"
}

make_cert() # name subject extensions signer
{
	echo "==> $1"
	openssl genrsa -out "$1.key.pem" 2048 2>/dev/null
	openssl req -new -batch -config openssl.conf \
		-key "$1.key.pem" \
		-out "$1.csr" \
		-subj "$2"
	openssl ca -batch -config openssl.conf \
		-in "$1.csr" \
		-out "$1.pem" \
		-extensions "$3" \
		-days 7300 \
		-notext \
		-cert "$4.pem" \
		-keyfile "$4.key.pem" 2>/dev/null
}

make_self_signed ca \
	"/C=EE/ST=Estonia/O=PAL/OU=PAL CA/CN=PAL Root CA" \
	v3_ca

make_cert intermediate \
	"/C=EE/ST=Estonia/O=PAL/OU=PAL CA/CN=PAL Intermediate CA" \
	v3_intermediate_ca ca

make_cert server \
	"/C=EE/ST=Estonia/O=PAL/OU=PAL Test/CN=pal.alt.ee" \
	server_cert intermediate

make_cert client \
	"/C=EE/ST=Estonia/O=PAL/OU=PAL Test/CN=pal.alt.ee/emailAddress=pal@alt.ee" \
	client_cert intermediate

make_self_signed self_signed \
	"/CN=Test" \
	v3_self_signed \
	36500

make_self_signed no_cn \
	"/O=PAL/OU=PAL Test" \
	v3_self_signed \
	36500

make_ec_self_signed empty_dn \
	"/" \
	v3_self_signed \
	36500

# ----- PKCS#12 ----------------------------------------------------------------

echo "==> PKCS#12"

# Chain order: identity cert (server) + extra cert (client) + intermediate + CA.
# client is included to enable predicate tests that match multiple certs.
#
# PBE-SHA1-3DES for WinCrypt compatibility. Private key is unencrypted in
# transit (test-only) but wrapped in the PKCS#12 structure.
cat client.pem intermediate.pem ca.pem > chain.pem
openssl pkcs12 -export \
	-inkey server.key.pem \
	-in server.pem \
	-certfile chain.pem \
	-passout pass:"" \
	-certpbe PBE-SHA1-3DES \
	-keypbe PBE-SHA1-3DES \
	-macalg SHA1 \
	-out pkcs12.p12

# ----- Helper functions -------------------------------------------------------

fingerprint() # pem
{
	openssl x509 -fingerprint -sha1 -noout -in "$1" \
		| sed 's/.*Fingerprint=//' | tr -d ':' | tr '[:upper:]' '[:lower:]'
}

serial() # pem
{
	openssl x509 -serial -noout -in "$1" \
		| sed 's/serial=//' | tr '[:upper:]' '[:lower:]'
}

common_name() # pem
{
	openssl x509 -subject -noout -nameopt multiline -in "$1" \
		| grep commonName | sed 's/.*= //' || true
}

pem_lines() # pem -- emit cert as C++ adjacent string literals, one per line
{
	awk 'NR>1{print "\t\t\"" prev "\\n\""} {prev=$0} END{print "\t\t\"" prev "\\n\""}' "$1"
}

emit_info() # name [size_bits=2048] [max_block_size=256] [algorithm=rsa]
{
	local fp sn cn pem
	fp=$(fingerprint "$1.pem")
	sn=$(serial "$1.pem")
	cn=$(common_name "$1.pem")
	pem=$(pem_lines "$1.pem")
	cat << EOF
constexpr info $1 =
{
	.version = 3,
	.common_name = "$cn",
	.subject_name = "",
	.subject_alternative_name = "",
	.issuer_name = "",
	.issuer_alternative_name = "",
	.serial_number = "$sn",
	.fingerprint = "$fp",
	.size_bits = ${2:-2048},
	.max_block_size = ${3:-256},
	.algorithm = pal::crypto::key_algorithm::${4:-rsa},
	.pem =
$pem,
};

EOF
}

# ----- Emit test_pkcs12.hpp ---------------------------------------------------

PKCS12_OUT="$REPO_ROOT/pal/crypto/test_pkcs12.hpp"
echo "==> Writing $PKCS12_OUT"

{
	echo "// Generated by scripts/test-certs/run.sh -- see that script for regeneration instructions."
	echo "// clang-format off"
	echo "#pragma once"
	echo ""
	echo "namespace pal_test::cert"
	echo "{"
	echo ""
	echo "constexpr uint8_t pkcs12_data[] ="
	echo "{"
	xxd -i pkcs12.p12 \
		| grep -v '^unsigned' \
		| sed 's/^  /\t/'
	echo ""
	echo "} // namespace pal_test::cert"
} > "$PKCS12_OUT"

# ----- Emit test_certs.hpp ----------------------------------------------------

CERTS_OUT="$REPO_ROOT/pal/crypto/test_certs.hpp"
echo "==> Writing $CERTS_OUT"

{
	echo "// Generated by scripts/test-certs/run.sh -- see that script for regeneration instructions."
	echo "// clang-format off"
	echo "#pragma once"
	echo ""
	echo "namespace pal_test::cert"
	echo "{"
	echo ""
	emit_info ca
	emit_info intermediate
	emit_info server
	emit_info client
	emit_info self_signed
	emit_info no_cn
	emit_info empty_dn 256 72 ec
	echo "constexpr const info *data[] = { &ca, &intermediate, &server, &client, &self_signed, &no_cn, &empty_dn };"
	echo ""
	echo "} // namespace pal_test::cert"
} > "$CERTS_OUT"

echo "==> Done: run 'ninja smoke', then copy subject_name/issuer_name/*_alternative_name values from failing tests into $CERTS_OUT"
