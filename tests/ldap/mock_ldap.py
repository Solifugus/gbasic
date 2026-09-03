#!/usr/bin/env python3
"""A minimal LDAP responder: real BER over a real socket, for testing a client.

It is NOT a directory. It parses enough of BindRequest and SearchRequest to
answer them, and returns entries it was configured with. That is deliberate --
what these tests check is what the gBASIC client does with well-formed
responses, not whether we can reimplement OpenLDAP.

Configured by argv: port, then a directive per user/entry.
"""
import socket, socketserver, ssl, sys, threading

# ---------------------------------------------------------------- BER
def enc_len(n):
    if n < 0x80:
        return bytes([n])
    b = n.to_bytes((n.bit_length() + 7) // 8, "big")
    return bytes([0x80 | len(b)]) + b

def tlv(tag, body):
    return bytes([tag]) + enc_len(len(body)) + body

def enc_int(v):
    n = max(1, (v.bit_length() + 8) // 8)
    return tlv(0x02, v.to_bytes(n, "big"))

def enc_enum(v):
    return tlv(0x0A, bytes([v]))

def enc_str(s):
    return tlv(0x04, s.encode() if isinstance(s, str) else s)

def enc_seq(*parts):
    return tlv(0x30, b"".join(parts))

def enc_set(*parts):
    return tlv(0x31, b"".join(parts))

# ---------------------------------------------------------------- decode
def read_len(buf, i):
    n = buf[i]; i += 1
    if n < 0x80:
        return n, i
    k = n & 0x7F
    return int.from_bytes(buf[i:i + k], "big"), i + k

def parse_msg(buf):
    """-> (messageID, op_tag, op_body) for one LDAPMessage."""
    assert buf[0] == 0x30
    total, i = read_len(buf, 1)
    assert buf[i] == 0x02
    ln, i = read_len(buf, i + 1)
    msgid = int.from_bytes(buf[i:i + ln], "big"); i += ln
    op_tag = buf[i]
    ln, i = read_len(buf, i + 1)
    return msgid, op_tag, buf[i:i + ln]

def parse_bind(body):
    """version, name, simple-password."""
    i = 0
    assert body[i] == 0x02
    ln, i = read_len(body, i + 1); i += ln          # version
    assert body[i] == 0x04
    ln, i = read_len(body, i + 1)
    dn = body[i:i + ln].decode(); i += ln
    tag = body[i]
    ln, i = read_len(body, i + 1)
    pw = body[i:i + ln].decode() if tag == 0x80 else ""
    return dn, pw

def skip_tlv(buf, i):
    ln, j = read_len(buf, i + 1)
    return j + ln

def parse_search(body):
    """base, scope, raw filter bytes, and the REQUESTED ATTRIBUTE LIST.

    The attribute list is parsed rather than ignored so a test can tell a
    client that sends one from a client that does not -- without it, asking
    for two attributes and asking for none look identical from here.
    """
    i = 0
    assert body[i] == 0x04
    ln, i = read_len(body, i + 1)
    base = body[i:i + ln].decode(); i += ln
    assert body[i] == 0x0A
    ln, i = read_len(body, i + 1)
    scope = body[i]; i += ln
    for _ in range(4):                       # deref, sizeLimit, timeLimit, typesOnly
        i = skip_tlv(body, i)
    fstart = i
    i = skip_tlv(body, i)                    # filter
    filt = body[fstart:i]
    wanted = []
    if i < len(body) and body[i] == 0x30:
        ln, j = read_len(body, i + 1)
        end = j + ln
        while j < end:
            aln, k = read_len(body, j + 1)
            wanted.append(body[k:k + aln].decode())
            j = k + aln
    return base, scope, filt, wanted

# ---------------------------------------------------------------- responses
LDAP_SUCCESS = 0
LDAP_NO_SUCH_OBJECT = 32
LDAP_INVALID_CREDENTIALS = 49

def result(tag, code, msg=""):
    return enc_seq(enc_int(0), tlv(tag, enc_enum(code) + enc_str("") + enc_str(msg)))

def result_for(msgid, tag, code, msg=""):
    return enc_seq(enc_int(msgid), tlv(tag, enc_enum(code) + enc_str("") + enc_str(msg)))

def search_entry(msgid, dn, attrs):
    parts = []
    for name, values in attrs.items():
        parts.append(enc_seq(enc_str(name), enc_set(*[enc_str(v) for v in values])))
    body = enc_str(dn) + enc_seq(*parts)
    return enc_seq(enc_int(msgid), tlv(0x64, body))

# ---------------------------------------------------------------- config
USERS = {}      # dn -> password
ENTRIES = []    # (dn, {attr: [values]})

class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        sock = self.request
        buf = b""
        while True:
            try:
                chunk = sock.recv(8192)
            except Exception:
                return
            if not chunk:
                return
            buf += chunk
            while len(buf) > 2:
                try:
                    total, hdr = read_len(buf, 1)
                except Exception:
                    break
                if len(buf) < hdr + total:
                    break
                msg, buf = buf[:hdr + total], buf[hdr + total:]
                try:
                    out = self.respond(msg)
                except Exception as e:
                    sys.stderr.write("mock: %s\n" % e)
                    return
                if out is None:
                    return
                sock.sendall(out)

    def respond(self, msg):
        msgid, tag, body = parse_msg(msg)
        if tag == 0x60:                                  # BindRequest
            dn, pw = parse_bind(body)
            ok = dn in USERS and USERS[dn] == pw
            return result_for(msgid, 0x61, LDAP_SUCCESS if ok
                              else LDAP_INVALID_CREDENTIALS,
                              "" if ok else "invalid credentials")
        if tag == 0x63:                                  # SearchRequest
            base, scope, filt, wanted = parse_search(body)
            out = b""
            for dn, attrs in ENTRIES:
                # Crude on purpose: an entry is returned when the filter's
                # bytes mention one of its own attribute values.
                if any(v.encode() in filt for vals in attrs.values() for v in vals):
                    # An empty list means "all", as LDAP defines it.
                    shown = attrs if not wanted else {
                        k: v for k, v in attrs.items()
                        if k.lower() in [w.lower() for w in wanted]}
                    out += search_entry(msgid, dn, shown)
            return out + result_for(msgid, 0x65, LDAP_SUCCESS)
        if tag == 0x42:                                  # UnbindRequest
            return None
        return result_for(msgid, 0x65, LDAP_NO_SUCH_OBJECT, "unsupported")

class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

def main():
    port = int(sys.argv[1])
    mode = sys.argv[2] if len(sys.argv) > 2 else "plain"
    certdir = sys.argv[3] if len(sys.argv) > 3 else None
    USERS["cn=alice,ou=people,dc=example,dc=com"] = "correct horse"
    USERS["cn=admin,dc=example,dc=com"] = "adminpw"
    ENTRIES.append(("cn=alice,ou=people,dc=example,dc=com",
                    {"uid": ["alice"], "cn": ["Alice Example"],
                     "mail": ["alice@example.com"],
                     "memberOf": ["cn=staff,ou=groups,dc=example,dc=com",
                                  "cn=finance,ou=groups,dc=example,dc=com"]}))
    ENTRIES.append(("cn=bob,ou=people,dc=example,dc=com",
                    {"uid": ["bob"], "cn": ["Bob Example"],
                     "memberOf": ["cn=staff,ou=groups,dc=example,dc=com"]}))
    srv = Server(("127.0.0.1", port), Handler)
    if mode == "ldaps":
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(certdir + "/server.crt", certdir + "/server.key")
        srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    sys.stderr.write("ready\n"); sys.stderr.flush()
    srv.serve_forever()

if __name__ == "__main__":
    main()
