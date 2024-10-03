#!/bin/sh

set -e

#
# https://jamielinux.com/docs/openssl-certificate-authority/index.html
#

rm -rf db
mkdir -p db/newcerts
touch db/index.txt
echo 1000 > db/serial

OPENSSL_CONF=db/openssl.conf #{{{1
cat >$OPENSSL_CONF <<OPENSSL_CONF
[ ca ]
default_ca = CA_default

[ CA_default ]
dir = db
new_certs_dir = \$dir/newcerts
database = \$dir/index.txt
serial = \$dir/serial
RANDFILE = \$dir/.rnd
default_md = sha256
name_opt = ca_default
cert_opt = ca_default
preserve = no
policy = policy_match
copy_extensions = copy

[ policy_match ]
C = match
ST = match
O = match
OU = optional
CN = supplied
emailAddress = optional

[ policy_anything ]
C = optional
ST = match
O = optional
OU = optional
CN = supplied
emailAddress = optional

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
issuerAltName = DNS:ca.pal.alt.ee,URI:https://ca.pal.alt.ee/path
subjectAltName = @server_alt_names
authorityKeyIdentifier = keyid,issuer:always
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
subjectAltName = @client_alt_names
authorityKeyIdentifier = keyid,issuer
keyUsage = critical, nonRepudiation, digitalSignature, keyEncipherment
extendedKeyUsage = clientAuth, emailProtection

[ client_alt_names ]
email.1 = pal@alt.ee
DNS.1 = client.pal.alt.ee
OPENSSL_CONF

_make_key() #{{{1
{
  echo "*** Key: $name ***"
  rm -f $name.key.pem
  openssl genrsa -aes256 \
    -out $name.key.pem \
    -passout pass:$password \
    $bits
  chmod 400 $name.key.pem
}

_make_certificate_request() #{{{1
{
  echo "*** Certificate request: $name ***"
  rm -f $name.csr.pem
  openssl req -new -sha256 -batch -config $OPENSSL_CONF \
    -out $name.csr.pem \
    -subj "$subject" \
    -passin pass:$password \
    -key $name.key.pem
}

make_self_signed_certificate() #{{{1
{
  eval $*

  _make_key

  echo "*** Certificate: $name (self-signed) ***"
  rm -f $name.pem
  openssl req -new -x509 -batch -config $OPENSSL_CONF \
    -out $name.pem \
    -subj "$subject" \
    -days 7300 \
    -sha256 \
    -extensions $extensions \
    -passin pass:$password \
    -key $name.key.pem
}

make_certificate() #{{{1
{
  eval $*

  _make_key
  _make_certificate_request

  echo "*** Certificate: $name ***"
  rm -f $name.pem
  openssl ca -batch -config $OPENSSL_CONF \
    -in $name.csr.pem \
    -out $name.pem \
    -extensions $extensions \
    -days 7300 \
    -notext \
    -cert $signer.pem \
    -passin pass:$signer_password \
    -keyfile $signer.key.pem
  chmod 444 $name.pem
  rm -f $name.csr.pem
}

# CA {{{1
make_self_signed_certificate \
  name=ca \
  subject="'/C=EE/ST=Estonia/O=PAL/OU=PAL CA/CN=PAL Root CA'" \
  extensions=v3_ca \
  bits=4096 \
  password=CaPassword

# Intermediate CA {{{1
make_certificate \
  name=intermediate \
  subject="'/C=EE/ST=Estonia/O=PAL/OU=PAL CA/CN=PAL Intermediate CA'" \
  extensions=v3_intermediate_ca \
  bits=4096 \
  password=IntermediatePassword \
  signer=ca \
  signer_password=CaPassword

# Server {{{1
make_certificate \
  name=server \
  subject="'/C=EE/ST=Estonia/O=PAL/OU=PAL Test/CN=pal.alt.ee'" \
  extensions=server_cert \
  bits=2048 \
  password=ServerPassword \
  signer=intermediate \
  signer_password=IntermediatePassword

# Client {{{1
make_certificate \
  name=client \
  subject="'/C=EE/ST=Estonia/O=PAL/OU=PAL Test/CN=pal.alt.ee/emailAddress=pal@alt.ee'" \
  extensions=client_cert \
  bits=2048 \
  password=ClientPassword \
  signer=intermediate \
  signer_password=IntermediatePassword

# PKCS#12 {{{1
# see https://stackoverflow.com/questions/70431528/mac-verification-failed-during-pkcs12-import-wrong-password-azure-devops
# as workaround using openssl for Linux/Windows and libressl on MacOS

cat server.key.pem client.pem intermediate.pem ca.pem server.pem \
  | openssl pkcs12 -export -passin pass:ServerPassword -passout pass:TestPassword \
  | xxd -i \
  | pbcopy

cat server.key.pem client.pem intermediate.pem ca.pem server.pem \
  | openssl pkcs12 -export -passin pass:ServerPassword -passout pass:"" \
  | xxd -i \
  | pbcopy
