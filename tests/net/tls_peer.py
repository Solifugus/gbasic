#!/usr/bin/env python3
"""The TLS end tests/run_net.sh puts against net: Python's ssl module, a TLS
implementation net did not write.

    tls_peer.py serve CERTDIR PORTS_FILE
        TLS servers for tests/net/tls_client.bas, on OS-assigned ports written
        to PORTS_FILE as JSON. Runs until killed.
    tls_peer.py client CHECK CERTDIR PORT...
        One check against tests/net/tls_server.bas; prints PASS or FAIL and
        exits non-zero on a failure.

CERTDIR holds what the runner generated: ca.pem, server.pem/key (localhost,
127.0.0.1), alt.pem/key (alt.test), stranger.pem/key (self-signed, so an
unknown issuer), client.pem/key (a client certificate the CA signed),
stranger_client.pem/key, and expired.pem/key when this openssl can backdate.
"""
import hashlib
import json
import os
import socket
import ssl
import struct
import sys
import threading
import time


def fail(msg):
    print("FAIL " + msg)
    sys.exit(1)


def path(d, name):
    return os.path.join(d, name)


def server_ctx(d, name, client_ca=None):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(path(d, name + ".pem"), path(d, name + ".key"))
    if client_ca:
        ctx.verify_mode = ssl.CERT_REQUIRED
        ctx.load_verify_locations(client_ca)
    return ctx


def echo(conn):
    try:
        while True:
            data = conn.recv(65536)
            if not data:
                break
            conn.sendall(data)
    except (OSError, ssl.SSLError):
        pass
    finally:
        try:
            conn.close()
        except OSError:
            pass


def recv_line(conn):
    data = b""
    while not data.endswith(b"\n"):
        chunk = conn.recv(1)
        if not chunk:
            return None
        data += chunk
    return data


def recv_exactly(conn, n):
    data = b""
    while len(data) < n:
        chunk = conn.recv(n - len(data))
        if not chunk:
            return None
        data += chunk
    return data


def common_name(cert):
    for rdn in cert.get("subject", ()):
        for key, value in rdn:
            if key == "commonName":
                return value
    return "none"


def serve(d, ports_file):
    def tls_service(ctx, greet=None):
        def handle(raw):
            try:
                conn = ctx.wrap_socket(raw, server_side=True)
            except (OSError, ssl.SSLError):
                raw.close()
                return
            if greet:
                try:
                    conn.sendall(greet(conn))
                except (OSError, ssl.SSLError):
                    conn.close()
                    return
            echo(conn)
        return handle

    def starttls_service(ctx):
        def handle(raw):
            try:
                line = recv_line(raw)
                if line == b"STARTTLS\n":
                    raw.sendall(b"OK\n")
                elif line == b"INJECT\n":
                    # The reply and the attacker's bytes in ONE send, so they
                    # arrive together, ahead of any handshake.
                    raw.sendall(b"OK\nINJECTED\n")
                else:
                    raw.close()
                    return
                conn = ctx.wrap_socket(raw, server_side=True)
            except (OSError, ssl.SSLError):
                raw.close()
                return
            echo(conn)
        return handle

    def silent_service(raw):
        # Accepts, then says nothing: a handshake against it can only time out.
        time.sleep(30)
        raw.close()

    def pg_service(ctx):
        def handle(raw):
            try:
                request = recv_exactly(raw, 8)
                if request != struct.pack(">II", 8, 80877103):
                    raw.close()
                    return
                raw.sendall(b"S")
                conn = ctx.wrap_socket(raw, server_side=True)
            except (OSError, ssl.SSLError):
                raw.close()
                return
            echo(conn)
        return handle

    services = {
        "echo": tls_service(server_ctx(d, "server")),
        "stranger": tls_service(server_ctx(d, "stranger")),
        "mtls": tls_service(server_ctx(d, "server", path(d, "ca.pem")),
                            lambda c: ("client:" + common_name(c.getpeercert()) + "\n").encode()),
        "starttls": starttls_service(server_ctx(d, "server")),
        "pg": pg_service(server_ctx(d, "server")),
        "silent": silent_service,
    }
    if os.path.exists(path(d, "expired.pem")):
        services["expired"] = tls_service(server_ctx(d, "expired"))
    ports = {}
    for name, handler in services.items():
        s = socket.socket()
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind(("127.0.0.1", 0))
        s.listen(64)
        ports[name] = s.getsockname()[1]

        def loop(s=s, handler=handler):
            while True:
                try:
                    raw, _ = s.accept()
                except OSError:
                    return
                threading.Thread(target=handler, args=(raw,), daemon=True).start()
        threading.Thread(target=loop, daemon=True).start()
    with open(ports_file + ".tmp", "w", newline="\n", encoding="utf-8") as f:
        json.dump(ports, f)
    os.rename(ports_file + ".tmp", ports_file)
    while True:
        time.sleep(3600)


# ---- client checks against tests/net/tls_server.bas ------------------------

def client_ctx(d, cert=None):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.load_verify_locations(path(d, "ca.pem"))
    if cert:
        ctx.load_cert_chain(path(d, cert + ".pem"), path(d, cert + ".key"))
    return ctx


def tls_connect(d, port, name="localhost", cert=None, ragged_ok=True):
    raw = socket.create_connection(("127.0.0.1", port), timeout=20)
    return client_ctx(d, cert).wrap_socket(raw, server_hostname=name, suppress_ragged_eofs=ragged_ok)


def ask(conn, line):
    conn.sendall(line)
    got = recv_line(conn)
    if got is None:
        fail("the server closed instead of answering %r" % line)
    return got


def check_sni(d, port):
    a = tls_connect(d, port, "localhost")
    if common_name(a.getpeercert()) != "localhost":
        fail("no SNI match should get the default certificate, got %s" % a.getpeercert())
    who = ask(a, b"who\n")
    a.close()
    b = tls_connect(d, port, "alt.test")
    if common_name(b.getpeercert()) != "alt.test":
        fail("SNI alt.test got %s" % common_name(b.getpeercert()))
    who_alt = ask(b, b"who\n")
    b.close()
    if who != b"sni=localhost client=none\n":
        fail("who said %r" % who)
    if who_alt != b"sni=alt.test client=none\n":
        fail("who (alt.test) said %r" % who_alt)
    print("PASS SNI chooses the certificate by name, and the program reads the name the client asked for")


def check_client_cert(d, port):
    c = tls_connect(d, port, "localhost", cert="client")
    who = ask(c, b"who\n")
    c.close()
    if who != b"sni=localhost client=CN=gBASIC Client\n":
        fail("with a client certificate, who said %r" % who)
    print("PASS a requested client certificate reaches the program as the peer's certificate")


def check_split(d, port):
    c = tls_connect(d, port)
    for b in b"alpha\nbe\x00ta\n":
        c.sendall(bytes([b]))         # one TLS record per byte
        time.sleep(0.002)
    got = recv_line(c) + recv_line(c)
    c.close()
    if got != b"echo:alpha\necho:be\x00ta\n":
        fail("one byte per record: %r" % got)
    print("PASS a line sent one TLS record per byte reassembles exactly, NUL included")


def check_plaintext(d, port):
    s = socket.create_connection(("127.0.0.1", port), timeout=10)
    s.sendall(b"GET / HTTP/1.0\r\n\r\n")
    try:
        rest = s.recv(100)
    except OSError:
        rest = b""
    s.close()
    if b"echo" in rest:
        fail("a plaintext client was answered: %r" % rest)
    print("PASS a plaintext client on the TLS port is not answered")


def check_starttls(d, port):
    raw = socket.create_connection(("127.0.0.1", port), timeout=20)
    raw.sendall(b"STARTTLS\n")
    if recv_line(raw) != b"OK\n":
        fail("the server did not agree to STARTTLS")
    c = client_ctx(d).wrap_socket(raw, server_hostname="localhost")
    got = ask(c, b"hi\n")
    c.close()
    if got != b"tls:hi\n":
        fail("after the upgrade: %r" % got)
    print("PASS the server side of start_tls: plaintext, then TLS on the same connection")


def check_pending(d, port):
    """A 1 KiB receive buffer against 16 KiB TLS records: the rest of each record
    is held inside libssl, where poll cannot see it."""
    c = tls_connect(d, port)
    want = os.urandom(200000)
    c.sendall(want)
    got = b""
    c.settimeout(15)
    try:
        while len(got) < len(want):
            chunk = c.recv(65536)
            if not chunk:
                break
            got += chunk
    except socket.timeout:
        pass
    c.close()
    if got != want:
        fail("echoed %d of %d bytes through a 1 KiB buffer (the rest stuck inside libssl?)" % (len(got), len(want)))
    print("PASS 200000 bytes through a 1 KiB buffer in 16 KiB records: nothing stuck inside libssl")


def check_flood(d, port):
    # A missing close_notify RAISES here (SSLEOFError) rather than reading as
    # an ordinary end, so the claim in the PASS line is measured.
    c = tls_connect(d, port, ragged_ok=False)
    c.sendall(b"flood\n")
    h = hashlib.sha256()
    total = 0
    c.settimeout(30)
    while True:
        try:
            chunk = c.recv(16384)
        except ssl.SSLZeroReturnError:
            break
        if not chunk:
            break
        h.update(chunk)
        total += len(chunk)
    c.close()
    want = b"0123456789abcdef" * 655360
    if total != len(want) or h.digest() != hashlib.sha256(want).digest():
        fail("received %d bytes over TLS, sha256 %s" % (total, h.hexdigest()))
    print("PASS a 10 MiB TLS write reached the reader whole and in order, and ended with close_notify")


def check_drip(d, port):
    """10 MiB written as 160 pieces, each when the server's queue drains, to a
    reader slow enough that libssl's writes are refused and retried."""
    raw = socket.socket()
    raw.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    raw.settimeout(60)
    raw.connect(("127.0.0.1", port))
    c = client_ctx(d).wrap_socket(raw, server_hostname="localhost", suppress_ragged_eofs=False)
    c.sendall(b"drip\n")
    h = hashlib.sha256()
    total = 0
    reads = 0
    while True:
        try:
            chunk = c.recv(4096)
        except ssl.SSLZeroReturnError:
            break
        if not chunk:
            break
        h.update(chunk)
        total += len(chunk)
        reads += 1
        if reads % 32 == 0:
            time.sleep(0.001)
    c.close()
    want = b"0123456789abcdef" * 4096 * 160
    if total != len(want) or h.digest() != hashlib.sha256(want).digest():
        fail("drip: received %d of %d bytes, sha256 %s" % (total, len(want), h.hexdigest()))
    print("PASS 10 MiB in 160 writes made on writable events reached a slow reader whole and in order")


def check_mute(d, port):
    """A client that connects to a TLS listener and never handshakes is closed
    at the listener's idle (2 seconds in tls_server.bas)."""
    s = socket.create_connection(("127.0.0.1", port), timeout=15)
    start = time.time()
    try:
        got = s.recv(100)
    except OSError:
        got = b""
    took = time.time() - start
    s.close()
    if got != b"" or took > 10:
        fail("a silent client was not closed at the idle timeout (%r after %.1f s)" % (got, took))
    print("PASS a client that never handshakes is closed at the listener's idle")


def check_one(d, port):
    c = tls_connect(d, port)
    got = ask(c, b"hi\n")
    c.close()
    if got != b"echo:hi\n":
        fail("the waited server answered %r" % got)
    print("PASS a waited TLS server accepted and answered")


def check_quit(d, port):
    c = tls_connect(d, port)
    c.sendall(b"quit\n")
    try:
        c.recv(10)
    except (OSError, ssl.SSLError):
        pass
    c.close()


def main():
    if sys.argv[1] == "serve":
        serve(sys.argv[2], sys.argv[3])
        return
    check, d, ports = sys.argv[2], sys.argv[3], [int(p) for p in sys.argv[4:]]
    checks = {
        "sni": check_sni, "client_cert": check_client_cert, "split": check_split,
        "plaintext": check_plaintext, "starttls": check_starttls, "pending": check_pending,
        "flood": check_flood, "quit": check_quit, "drip": check_drip, "mute": check_mute,
        "one": check_one,
    }
    if check not in checks:
        fail("unknown check " + check)
    checks[check](d, ports[0])


main()
