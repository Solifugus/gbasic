#!/usr/bin/env bash
# What verifying a SAML signature needs from gBASIC (Transward M31 §10):
# xml.canonicalize (Exclusive XML Canonicalization 1.0 over libxml2's c14n,
# an element chosen by ID or by name, its enveloped signature left out) and
# x509_parse (a certificate's names, dates, serial, fingerprint and key).
#
# NO EXPECTED BYTE COMES FROM libxml2. lxml would be the easy oracle and is
# libxml2 underneath, so it could only agree. Instead:
#   TIER c14n     -- tests/xml_sign/c14n_test.bas (floor 56), every answer written by
#                    hand from the specifications (exc-c14n §2.2's two
#                    documents, whose outputs the spec gives), and refusals
#                    (an ID held twice, a DTD, two enveloped signatures)
#   TIER x509     -- RSA, P-256 and Ed25519 certificates made by openssl per
#                    run, PEM and DER, each field held against what openssl
#                    itself reports (-subject/-issuer RFC2253, -serial,
#                    -fingerprint -sha256, -dates, -modulus, the SPKI's point)
#   TIER signed   -- an assertion signed the way an identity provider signs
#                    one: openssl digests and signs canonical bytes WRITTEN BY
#                    HAND, and gBASIC, given the document as an IdP would send
#                    it (empty elements, spaces in tags), must reproduce both
#                    and verify with the certificate's key; a tampered
#                    assertion and a wrapped one must fail
#   TIER valgrind -- c14n and x509 fixtures, bounded
# Skips without libxml2 or libcrypto in the build, or without openssl.
. "$(dirname "$0")/portable.sh"
set -uo pipefail
cd "$(dirname "$0")/.."

make >/dev/null 2>&1 || { echo "FAIL build"; exit 1; }
export GBASIC_PATH=stdlib
status=0
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; status=1; }

printf 'load xml\nprint "ok"\n' > "$tmp/probe.bas"
if [ "$(./gbasic "$tmp/probe.bas" 2>&1)" != ok ]; then
    printf 'SKIP run_xml_sign (this build has no xml module: nothing was measured)\n'; exit 0
fi
printf 'print has_builtin("x509_parse")\n' > "$tmp/probe2.bas"
have_crypto="$(./gbasic "$tmp/probe2.bas" 2>&1)"

echo "--- TIER c14n ---"
out="$(timeout -k 5 60 ./gbasic tests/xml_sign/c14n_test.bas 2>"$tmp/c14n.err" </dev/null)"
last="$(printf '%s\n' "$out" | tail -1)"
printf '%s\n' "$out" | grep '^MISMATCH' | sed 's/^/  /'
n="$(printf '%s\n' "$last" | sed -n 's/^ok \([0-9]*\) checks, 0 mismatches$/\1/p')"
if [ -z "$n" ] || [ "$n" -lt 56 ]; then
    fail "c14n (${last:-no verdict})"
elif [ -s "$tmp/c14n.err" ]; then
    sed 's/^/  /' "$tmp/c14n.err" | head -5; fail "c14n (the fixture wrote to stderr)"
else
    pass "c14n ($n checks, every answer by hand)"
fi

if [ "$have_crypto" != true ] || ! command -v openssl >/dev/null 2>&1; then
    printf 'SKIP x509 and signed (no libcrypto in the build, or no openssl): certificates and signatures were NOT measured\n'
    [ "$status" = 0 ] && echo "run_xml_sign: all checks that ran passed"
    exit $status
fi

echo "--- TIER x509 ---"
# a subject with an escape RFC 2253 must make (a comma) and an issuer that is
# not the subject (signed by a CA), so the two fields cannot be swapped unseen
openssl req -x509 -newkey rsa:2048 -nodes -keyout "$tmp/ca.key" -out "$tmp/ca.pem" -days 30 \
    -subj "/O=Test CA/CN=ca.example" 2>/dev/null
mkcert() { # name keyargs subject
    openssl req -newkey "$2" -nodes -keyout "$tmp/$1.key" -out "$tmp/$1.csr" -subj "$3" 2>/dev/null
    openssl x509 -req -in "$tmp/$1.csr" -CA "$tmp/ca.pem" -CAkey "$tmp/ca.key" -set_serial "$4" \
        -days 400 -out "$tmp/$1.pem" 2>/dev/null
    openssl x509 -in "$tmp/$1.pem" -outform DER -out "$tmp/$1.der"
}
mkcert rsa rsa:2048 "/C=US/O=Idp, Inc./CN=idp.example" 0x0123456789abcdef
mkcert rsa2 rsa:2048 "/CN=other.example" 9
mkcert p256 ec:<(openssl ecparam -name prime256v1) "/CN=p256.example" 7
openssl genpkey -algorithm ed25519 -out "$tmp/ed.key" 2>/dev/null
openssl req -new -key "$tmp/ed.key" -out "$tmp/ed.csr" -subj "/CN=ed.example" 2>/dev/null
openssl x509 -req -in "$tmp/ed.csr" -CA "$tmp/ca.pem" -CAkey "$tmp/ca.key" -set_serial 255 -days 400 \
    -out "$tmp/ed.pem" 2>/dev/null
openssl x509 -in "$tmp/ed.pem" -outform DER -out "$tmp/ed.der"
cat > "$tmp/x509.bas" <<'EOF'
for each f in [env("CERT")]
    for each form in ["pem", "der"]
        c = x509_parse(read(f + "." + form))
        print form + " subject=" + c.subject
        print form + " issuer=" + c.issuer
        print form + " serial=" + upper(hex_encode(c.serial))
        print form + " fingerprint=" + upper(hex_encode(c.fingerprint_sha256))
        print form + " der=" + string(c.der = read(f + ".der"))
        print form + " not_before=" + string(c.not_before)
        print form + " not_after=" + string(c.not_after)
        print form + " key_type=" + c.key_type
        if c.key_type = "rsa" then
            print form + " modulus=" + upper(hex_encode(c.public_key.n))
            print form + " exponent=" + hex_encode(c.public_key.e)
        else if c.key_type = "ec" then
            print form + " curve=" + c.public_key.curve
            print form + " point=" + hex_encode(c.public_key.public)
        else
            print form + " point=" + hex_encode(c.public_key)
        end if
    next
next
EOF
iso() { date -u -d "$1" '+%Y-%m-%d %H:%M:%S'; }
for k in rsa p256 ed; do
    got="$(CERT="$tmp/$k" ./gbasic "$tmp/x509.bas" 2>&1)"
    p="$tmp/$k.pem"
    nb="$(iso "$(openssl x509 -in "$p" -noout -startdate | sed 's/^notBefore=//')")"
    na="$(iso "$(openssl x509 -in "$p" -noout -enddate | sed 's/^notAfter=//')")"
    spki_hex="$(openssl x509 -in "$p" -noout -pubkey | openssl pkey -pubin -outform DER 2>/dev/null | od -An -tx1 -v | tr -d ' \n')"
    serial="$(openssl x509 -in "$p" -noout -serial | sed 's/^serial=//')"
    case "$k" in
        rsa) keylines="modulus=$(openssl x509 -in "$p" -noout -modulus | sed 's/^Modulus=//')
exponent=010001"; kt=rsa ;;
        p256) keylines="curve=p256
point=${spki_hex: -130}"; kt=ec ;;
        ed) keylines="point=${spki_hex: -64}"; kt=ed25519 ;;
    esac
    want=""
    for form in pem der; do
        want+="$form subject=$(openssl x509 -in "$p" -noout -subject -nameopt RFC2253 | sed 's/^subject=//')
$form issuer=$(openssl x509 -in "$p" -noout -issuer -nameopt RFC2253 | sed 's/^issuer=//')
$form serial=$(printf '%s' "$serial" | sed 's/^0*\(.\)/\1/' | awk '{ if (length($0) % 2) print "0" $0; else print }')
$form fingerprint=$(openssl x509 -in "$p" -noout -fingerprint -sha256 | sed 's/^.*=//; s/://g')
$form der=true
$form not_before=$nb
$form not_after=$na
$form key_type=$kt
$(printf '%s\n' "$keylines" | sed "s/^/$form /")
"
    done
    if [ "$got" = "${want%$'\n'}" ]; then
        pass "x509 $k (every field, PEM and DER, as openssl reports it)"
    else
        diff <(printf '%s\n' "${want%$'\n'}") <(printf '%s\n' "$got") | head -12 | sed 's/^/  /'
        fail "x509 $k"
    fi
done
printf 'on error goto next\nc = x509_parse(read("%s"))\nif error then\n    print error.message\nend if\nc = x509_parse(read("%s") + "x")\nif error then\n    print error.message\nend if\n' \
    "$tmp/rsa.key" "$tmp/rsa.der" > "$tmp/x509bad.bas"
got="$(./gbasic "$tmp/x509bad.bas" 2>&1)"
want="x509_parse: no X.509 certificate could be read from that (a PEM's BEGIN line must be CERTIFICATE; DER must be the certificate's bytes and nothing after)"
if [ "$got" = "$want"$'\n'"$want" ]; then
    pass "x509 refusals (a private key's PEM; DER with a byte after it)"
else
    printf '  %s\n' "$got"; fail "x509 refusals"
fi
# rsa_from_pem handed the certificate names it and points at x509_parse (DOGFOOD
# row 11), whose key is the private key's own; text that is neither keeps the
# passphrase message, so the new one is not simply every failure's
printf 'on error goto next\nk = rsa_from_pem(read("%s"))\nif error then\n    print error.message\nend if\nprint x509_parse(read("%s")).public_key.n = rsa_from_pem(read("%s")).n\nk = rsa_from_pem("junk")\nif error then\n    print error.message\nend if\n' \
    "$tmp/rsa.pem" "$tmp/rsa.pem" "$tmp/rsa.key" > "$tmp/rsacert.bas"
got="$(./gbasic "$tmp/rsacert.bas" 2>&1)"
want="rsa_from_pem: that text is an X.509 certificate, not a key; x509_parse(text).public_key is the key it holds
true
rsa_from_pem: no RSA key could be read from that text (an encrypted key needs its passphrase)"
if [ "$got" = "$want" ]; then
    pass "x509 rsa_from_pem on a certificate names it and x509_parse"
else
    printf '  %s\n' "$got"; fail "x509 rsa_from_pem on a certificate"
fi

echo "--- TIER signed ---"
# The canonical bytes, by hand: what exc-c14n makes of the assertion with its
# signature removed, and of SignedInfo, whose ds prefix is declared on the
# Signature above it and so must appear on SignedInfo itself.
DS=http://www.w3.org/2000/09/xmldsig#
printf '%s' '<saml:Assertion xmlns:saml="urn:oasis:names:tc:SAML:2.0:assertion" ID="_a1" IssueInstant="2026-11-08T00:00:00Z" Version="2.0"><saml:Issuer>https://idp.example</saml:Issuer><saml:Subject><saml:NameID>alice@example.com</saml:NameID></saml:Subject></saml:Assertion>' > "$tmp/assertion.c14n"
digest="$(openssl dgst -sha256 -binary "$tmp/assertion.c14n" | base64 -w0)"
signed_info() { # digest -> canonical SignedInfo
    printf '<ds:SignedInfo xmlns:ds="%s"><ds:CanonicalizationMethod Algorithm="http://www.w3.org/2001/10/xml-exc-c14n#"></ds:CanonicalizationMethod><ds:SignatureMethod Algorithm="http://www.w3.org/2001/04/xmldsig-more#rsa-sha256"></ds:SignatureMethod><ds:Reference URI="#_a1"><ds:Transforms><ds:Transform Algorithm="http://www.w3.org/2000/09/xmldsig#enveloped-signature"></ds:Transform><ds:Transform Algorithm="http://www.w3.org/2001/10/xml-exc-c14n#"></ds:Transform></ds:Transforms><ds:DigestMethod Algorithm="http://www.w3.org/2001/04/xmlenc#sha256"></ds:DigestMethod><ds:DigestValue>%s</ds:DigestValue></ds:Reference></ds:SignedInfo>' "$DS" "$1"
}
signed_info "$digest" > "$tmp/signedinfo.c14n"
sigval="$(openssl dgst -sha256 -sign "$tmp/rsa.key" -binary "$tmp/signedinfo.c14n" | base64 -w0)"
# the document as sent: a Response around it, empty elements, a space in a
# tag, the attributes out of order, a comment
response() { # assertion-subject -> the document
    cat <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<samlp:Response xmlns:samlp="urn:oasis:names:tc:SAML:2.0:protocol" ID="_r1" Version="2.0"><saml:Assertion Version="2.0" ID="_a1" xmlns:saml="urn:oasis:names:tc:SAML:2.0:assertion" IssueInstant="2026-11-08T00:00:00Z" ><saml:Issuer>https://idp.example</saml:Issuer><ds:Signature xmlns:ds="$DS"><ds:SignedInfo><ds:CanonicalizationMethod Algorithm="http://www.w3.org/2001/10/xml-exc-c14n#"/><ds:SignatureMethod Algorithm="http://www.w3.org/2001/04/xmldsig-more#rsa-sha256"/><ds:Reference URI="#_a1"><ds:Transforms><ds:Transform Algorithm="http://www.w3.org/2000/09/xmldsig#enveloped-signature"/><ds:Transform Algorithm="http://www.w3.org/2001/10/xml-exc-c14n#"/></ds:Transforms><ds:DigestMethod Algorithm="http://www.w3.org/2001/04/xmlenc#sha256"/><ds:DigestValue>$digest</ds:DigestValue></ds:Reference></ds:SignedInfo><ds:SignatureValue>$sigval</ds:SignatureValue></ds:Signature><saml:Subject><!-- the user --><saml:NameID>$1</saml:NameID></saml:Subject></saml:Assertion>$2</samlp:Response>
EOF
}
response 'alice@example.com' '' > "$tmp/good.xml"
response 'mallory@example.com' '' > "$tmp/tampered.xml"
# wrapped: a second element holding the signed ID
response 'alice@example.com' '<saml:Assertion xmlns:saml="urn:oasis:names:tc:SAML:2.0:assertion" ID="_a1"><saml:Subject><saml:NameID>mallory@example.com</saml:NameID></saml:Subject></saml:Assertion>' > "$tmp/wrapped.xml"
cat > "$tmp/verify.bas" <<'EOF'
load xml
ds = "http://www.w3.org/2000/09/xmldsig#"
key = x509_parse(read(env("CERT"))).public_key
on error goto next
doc = read(env("DOC"))
a = xml.canonicalize(doc, { id: "_a1", exclude_signature: true })
if error then
    print "REFUSED " + error.message
else
    si = xml.canonicalize(doc, { id: "_a1", element: "SignedInfo", namespace: ds })
    if default(env("SHOW"), "") = "1" then
        write(env("OUT") + ".a", a)
        write(env("OUT") + ".si", si)
    end if
    tree = xml.parse(si)
    ' a path is a chain of CHILD names, so each is the full path (a miss is
    ' unknown, and xml.text of it raises)
    want = xml.text(xml.find(tree, "Reference/DigestValue"))
    sig = xml.text(xml.find(xml.parse(doc), "Assertion/Signature/SignatureValue"))
    print "digest " + string(base64_encode(sha256(a)) = want)
    print "signature " + string(rsa_verify(key, si, base64_decode(sig), "sha256"))
end if
EOF
got="$(CERT="$tmp/rsa.pem" DOC="$tmp/good.xml" SHOW=1 OUT="$tmp/got" ./gbasic "$tmp/verify.bas" 2>&1)"
if [ "$got" = $'digest true\nsignature true' ] && cmp -s "$tmp/got.a" "$tmp/assertion.c14n" &&
   cmp -s "$tmp/got.si" "$tmp/signedinfo.c14n"; then
    pass "signed (both canonical forms byte for byte as written by hand; openssl's digest and signature verify)"
else
    printf '  %s\n' "$got"
    cmp "$tmp/got.a" "$tmp/assertion.c14n" | sed 's/^/  /'
    cmp "$tmp/got.si" "$tmp/signedinfo.c14n" | sed 's/^/  /'
    fail "signed"
fi
got="$(CERT="$tmp/rsa.pem" DOC="$tmp/tampered.xml" ./gbasic "$tmp/verify.bas" 2>&1)"
if [ "$got" = $'digest false\nsignature true' ]; then
    pass "signed: a changed NameID fails its digest, under an intact signature"
else
    printf '  %s\n' "$got"; fail "signed: tampered"
fi
got="$(CERT="$tmp/rsa.pem" DOC="$tmp/wrapped.xml" ./gbasic "$tmp/verify.bas" 2>&1)"
case "$got" in
    "REFUSED xml.canonicalize: 2 elements have ID=\"_a1\""*) pass "signed: a wrapped assertion (the signed ID twice) is refused" ;;
    *) printf '  %s\n' "$got"; fail "signed: wrapped" ;;
esac
got="$(CERT="$tmp/rsa2.pem" DOC="$tmp/good.xml" ./gbasic "$tmp/verify.bas" 2>&1)"
if [ "$got" = $'digest true\nsignature false' ]; then
    pass "signed: another RSA certificate's key does not verify it (its digest still matches)"
else
    printf '  %s\n' "$got"; fail "signed: another certificate's key"
fi

echo "--- TIER valgrind ---"
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    timeout -k 5 300 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic tests/xml_sign/c14n_test.bas' \
        >/dev/null 2>"$tmp/vg.err" </dev/null
    rc=$?
    CERT="$tmp/rsa" timeout -k 5 300 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic "$0"' "$tmp/x509.bas" \
        >/dev/null 2>"$tmp/vg2.err" </dev/null
    rc2=$?
    if [ "$rc" = 124 ] || [ "$rc" = 137 ] || [ "$rc2" = 124 ] || [ "$rc2" = 137 ]; then
        fail "valgrind (did not finish in 300 s)"
    elif [ "$rc" = "$VG_EXIT" ] || [ "$rc2" = "$VG_EXIT" ]; then
        cat "$tmp/vg.err" "$tmp/vg2.err" | head -40; fail "valgrind"
    else
        pass "valgrind (c14n and x509: no definite leak or invalid access)"
    fi
else
    printf 'SKIP valgrind (unavailable)\n'
fi

[ "$status" = 0 ] && echo "run_xml_sign: all checks passed"
exit $status
