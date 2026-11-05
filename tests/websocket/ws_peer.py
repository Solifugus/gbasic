"""The other end of gBASIC's websocket library (N4): Python's `websockets`
package for the well-formed paths, and raw sockets with framing written here
for what a conforming library will not send.

    ws_peer.py CHECK PORT [ARG]     drive tests/websocket/ws_server.bas
    ws_peer.py serve PORTS_FILE [CERTDIR]
                                    servers for tests/websocket/ws_client.bas

Each check prints PASS lines, or FAIL and exits 1.
"""
import asyncio
import base64
import hashlib
import json
import os
import socket
import ssl
import struct
import sys
import threading
import urllib.request

import websockets
from websockets.asyncio.server import serve as ws_serve
from websockets.exceptions import ConnectionClosed

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


def fail(msg):
    print("FAIL " + msg)
    sys.exit(1)


def ok(msg):
    print("PASS " + msg)


def accept_for(key):
    return base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()


# ---- raw framing, written here and not taken from the library ----------------

def frame(opcode, payload, mask=True, fin=True, rsv=0):
    b0 = (0x80 if fin else 0) | (rsv << 4) | opcode
    n = len(payload)
    mbit = 0x80 if mask else 0
    if n < 126:
        head = struct.pack(">BB", b0, mbit | n)
    elif n < 65536:
        head = struct.pack(">BBH", b0, mbit | 126, n)
    else:
        head = struct.pack(">BBQ", b0, mbit | 127, n)
    if not mask:
        return head + payload
    key = os.urandom(4)
    return head + key + bytes(b ^ key[i % 4] for i, b in enumerate(payload))


def read_exact(s, n):
    out = b""
    while len(out) < n:
        d = s.recv(n - len(out))
        if not d:
            raise EOFError("closed after %d of %d bytes" % (len(out), n))
        out += d
    return out


def read_frame(s):
    b0, b1 = read_exact(s, 2)
    n = b1 & 0x7F
    if n == 126:
        n = struct.unpack(">H", read_exact(s, 2))[0]
    elif n == 127:
        n = struct.unpack(">Q", read_exact(s, 8))[0]
    key = read_exact(s, 4) if b1 & 0x80 else None
    data = read_exact(s, n)
    if key:
        data = bytes(b ^ key[i % 4] for i, b in enumerate(data))
    return b0 & 0x0F, bool(b0 & 0x80), bool(b1 & 0x80), data


def raw_handshake(port, path="/echo", extra=b"", version="13"):
    s = socket.create_connection(("127.0.0.1", port), timeout=10)
    key = base64.b64encode(os.urandom(16)).decode()
    req = ("GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
           "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: %s\r\n\r\n" % (path, key, version)).encode()
    s.sendall(req + extra)
    head = b""
    while b"\r\n\r\n" not in head:
        d = s.recv(1)
        if not d:
            break
        head += d
    return s, head.decode("latin-1"), key


def close_code_of(s):
    """Read frames until a close; its code, or None if the socket just ended."""
    try:
        while True:
            op, fin, masked, data = read_frame(s)
            if op == 8:
                return struct.unpack(">H", data[:2])[0] if len(data) >= 2 else 1005
    except (EOFError, OSError):
        return None


# ---- checks against the gBASIC server ------------------------------------------

async def c_echo(port):
    async with websockets.connect("ws://127.0.0.1:%d/echo" % port, subprotocols=["other", "echo.v1"],
                                  max_size=None) as ws:
        if ws.subprotocol != "echo.v1":
            fail("echo: the protocol chosen was %r, want echo.v1" % ws.subprotocol)
        for text in ["hello", "", "héllo wörld €", "x" * 125, "y" * 126, "z" * 65535, "w" * 65536, "v" * 200000]:
            await ws.send(text)
            got = await ws.recv()
            if got != text:
                fail("echo: a %d-character text message came back as %r..." % (len(text), got[:20]))
        data = bytes(range(256)) * 4
        await ws.send(data)
        got = await ws.recv()
        if got != data or not isinstance(got, bytes):
            fail("echo: binary came back as %r" % type(got))
    ok("text and binary echoed at every length form (0, 125, 126, 65535, 65536, 200000), NUL and every byte")
    ok("  and the subprotocol the server offers was chosen from the client's list")


async def c_fragments(port):
    async with websockets.connect("ws://127.0.0.1:%d/echo" % port) as ws:
        await ws.send(["frag", "ment", "ed"])
        got = await ws.recv()
        if got != "fragmented":
            fail("fragments: got %r" % got)
        await ws.send([b"\x00\x01", b"\x02"])
        got = await ws.recv()
        if got != b"\x00\x01\x02":
            fail("fragments: binary got %r" % got)
    ok("a message sent as several fragments arrives whole, text and binary")


async def c_ping(port):
    async with websockets.connect("ws://127.0.0.1:%d/echo" % port) as ws:
        waiter = await ws.ping(b"are you there")
        await asyncio.wait_for(waiter, 5)
        await ws.send("after ping")
        if await ws.recv() != "after ping":
            fail("ping: the connection did not keep working")
    ok("a ping is answered with a pong, and the connection carries on")


async def c_close_client(port):
    ws = await websockets.connect("ws://127.0.0.1:%d/echo" % port)
    await ws.close(1000, "done here")
    if ws.close_code != 1000:
        fail("close: the server answered the closing handshake with %r" % ws.close_code)
    ok("a client's close (1000) is answered with a close frame: a clean closing handshake")


async def c_close_server(port):
    ws = await websockets.connect("ws://127.0.0.1:%d/echo" % port)
    await ws.send("close")
    try:
        await asyncio.wait_for(ws.recv(), 5)
        fail("close: a message came where a close was expected")
    except ConnectionClosed:
        pass
    if ws.close_code != 4000 or ws.close_reason != "asked to":
        fail("close: got %r %r, want 4000 'asked to'" % (ws.close_code, ws.close_reason))
    ok("websocket.close(c, 4000, reason) reaches the client with its code and reason")


async def c_stats(port, want):
    async with websockets.connect("ws://127.0.0.1:%d/echo" % port) as ws:
        await ws.send("stats")
        got = await ws.recv()
    fields = dict(part.split("=", 1) for part in got.split())
    seen = fields.get("closed", "").split(",")
    for code in want.split(","):
        if code not in seen:
            fail("stats: the server reports closed codes %s, want %s among them" % (sorted(set(seen)), code))
    ok("the server was told each close by its code: " + want)


async def c_many(port, n):
    async def one(i):
        async with websockets.connect("ws://127.0.0.1:%d/echo" % port) as ws:
            await ws.send("n%d" % i)
            if await ws.recv() != "n%d" % i:
                fail("many: connection %d got another's answer" % i)
    await asyncio.gather(*(one(i) for i in range(n)))
    ok("%d WebSockets at once, each answered its own message" % n)


def c_plain(port):
    body = urllib.request.urlopen("http://127.0.0.1:%d/plain" % port, timeout=5).read()
    if body != b"plain http still works":
        fail("plain: %r" % body)
    ok("an ordinary route beside the upgrade route still answers")


def c_refusals(port):
    try:
        urllib.request.urlopen("http://127.0.0.1:%d/echo" % port, timeout=5)
        fail("refusals: a plain GET of the WebSocket route was answered 200")
    except urllib.error.HTTPError as e:
        if e.code != 400:
            fail("refusals: a plain GET got %d, want 400" % e.code)
    s, head, _ = raw_handshake(port, version="8")
    s.close()
    if not head.startswith("HTTP/1.1 426") or "sec-websocket-version: 13" not in head.lower():
        fail("refusals: version 8 got %r" % head.split("\r\n")[0])
    s, head, key = raw_handshake(port)
    if not head.startswith("HTTP/1.1 101") or ("sec-websocket-accept: " + accept_for(key)).lower() not in head.lower():
        fail("refusals: the accept value is not RFC 6455's for the key sent: %r" % head)
    s.close()
    ok("a plain GET of the route is 400, version 8 is 426 naming 13, and the accept value is the RFC's")


def c_bytewise(port):
    s, head, _ = raw_handshake(port)
    msg = "one byte at a time".encode()
    for b in frame(1, msg):
        s.sendall(bytes([b]))
    op, fin, masked, data = read_frame(s)
    if (op, data, masked) != (1, msg, False):
        fail("bytewise: got opcode %d %r masked=%s" % (op, data, masked))
    s.close()
    ok("a frame sent one byte per send reassembles, and the server's frames are unmasked")


def c_early(port):
    first = frame(1, b"sent with the handshake")
    s, head, _ = raw_handshake(port, extra=first)
    op, fin, masked, data = read_frame(s)
    if data != b"sent with the handshake":
        fail("early: got %r" % data)
    s.close()
    ok("a frame in the same write as the handshake is the connection's first message")


def c_violations(port):
    cases = [
        ("an unmasked client frame", "/echo", frame(1, b"hi", mask=False), 1002),
        ("a reserved bit set", "/echo", frame(1, b"hi", rsv=4), 1002),
        ("text that is not UTF-8", "/echo", frame(1, b"\xc0\xaf"), 1007),
        ("a continuation with nothing to continue", "/echo", frame(0, b"x"), 1002),
        ("a fragmented ping", "/echo", frame(9, b"x", fin=False), 1002),
        ("a ping of 126 bytes", "/echo", frame(9, b"p" * 126), 1002),
        ("a reserved opcode", "/echo", frame(3, b"x"), 1002),
        ("close code 1005 on the wire", "/echo", frame(8, struct.pack(">H", 1005)), 1002),
        ("a new message inside a fragmented one", "/echo", frame(1, b"a", fin=False) + frame(1, b"b"), 1002),
        ("a message past max_message (1024)", "/small", frame(2, b"m" * 2000), 1009),
        ("fragments past max_message", "/small", frame(2, b"m" * 600, fin=False) + frame(0, b"m" * 600), 1009),
    ]
    for label, path, data, want in cases:
        s, head, _ = raw_handshake(port, path=path)
        if not head.startswith("HTTP/1.1 101"):
            fail("violations: %s: no upgrade: %r" % (label, head[:40]))
        s.sendall(data)
        got = close_code_of(s)
        s.close()
        if got != want:
            fail("violations: %s closed with %r, want %d" % (label, got, want))
    ok("%d protocol violations each closed with the code that names it (1002, 1007, 1009)" % len(cases))


def c_tls(port, certdir):
    ctx = ssl.create_default_context(cafile=os.path.join(certdir, "ca.pem"))

    async def go():
        async with websockets.connect("wss://localhost:%d/echo" % port, ssl=ctx) as ws:
            await ws.send("over tls")
            if await ws.recv() != "over tls":
                fail("tls: no echo")
            await ws.send("y" * 100000)
            if await ws.recv() != "y" * 100000:
                fail("tls: the large message did not survive")
            await ws.send("tls")
            facts = await ws.recv()
            if "TLSv1" not in facts:
                fail("tls: the upgraded connection has no tls facts: %r" % facts)
            await ws.send("flood")
            await asyncio.sleep(1)          # a slow reader: the server's queue backs up
            for i in range(1, 101):
                got = await asyncio.wait_for(ws.recv(), 30)
                if got != chr(64 + i % 26) * 100000:
                    fail("tls: flood message %d came back wrong (%d bytes)" % (i, len(got)))
    asyncio.run(go())
    ok("wss: the TLS session moved with the socket from the webserver to net, and c.tls says so")


def c_unwatched(port):
    s, head, _ = raw_handshake(port, path="/ws")
    s.close()
    if not head.startswith("HTTP/1.1 500"):
        fail("unwatched: an upgrade nobody watches for was answered %r" % head.split("\r\n")[0])
    ok("an upgrade in a program not watching net.events is a 500, not a socket handed to nowhere")


def c_quit(port):
    urllib.request.urlopen("http://127.0.0.1:%d/quit" % port, timeout=5).read()
    print("INFO quit sent")


# ---- servers for the gBASIC client ---------------------------------------------

async def handler(ws):
    path = ws.request.path
    if path == "/echo":
        async for m in ws:
            await ws.send(m)
    elif path == "/frag":
        await ws.send(["frag", "ment", "ed"])
        await ws.send([b"\x01", b"\x02\x03"])
        async for m in ws:
            await ws.send(m)
    elif path == "/ping":
        waiter = await ws.ping(b"from server")
        try:
            await asyncio.wait_for(waiter, 5)
            await ws.send("pong ok")
        except asyncio.TimeoutError:
            await ws.send("no pong")
        async for m in ws:
            pass
    elif path == "/bye":
        await ws.recv()
        await ws.close(4001, "server bye")
    elif path == "/headers":
        h = ws.request.headers
        await ws.send("%s|%s" % (h.get("X-Test", ""), h.get("Sec-WebSocket-Protocol", "")))
        async for m in ws:
            pass


def raw_server():
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(16)

    def one(c):
        try:
            head = b""
            while b"\r\n\r\n" not in head:
                d = c.recv(4096)
                if not d:
                    return
                head += d
            text = head.decode("latin-1")
            path = text.split(" ")[1]
            key = [l.split(":", 1)[1].strip() for l in text.split("\r\n") if l.lower().startswith("sec-websocket-key:")][0]
            if path == "/noupgrade":
                c.sendall(b"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nno")
            elif path == "/badaccept":
                c.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: %s\r\n\r\n" % accept_for("not the key")).encode())
            elif path == "/masked":
                c.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: %s\r\n\r\n" % accept_for(key)).encode() + frame(1, b"masked", mask=True))
                close_code_of(c)
            elif path == "/drop":
                c.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Accept: %s\r\n\r\n" % accept_for(key)).encode() + frame(1, b"then gone", mask=False))
            c.close()
        except Exception:
            c.close()

    def loop():
        srv.settimeout(120)
        try:
            while True:
                c, _ = srv.accept()
                threading.Thread(target=one, args=(c,), daemon=True).start()
        except Exception:
            pass
    threading.Thread(target=loop, daemon=True).start()
    return srv.getsockname()[1]


async def serve_main(ports_file, certdir):
    ports = {"raw": raw_server()}
    async with ws_serve(handler, "127.0.0.1", 0) as plain:
        ports["ws"] = plain.sockets[0].getsockname()[1]
        tls_srv = None
        if certdir:
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ctx.load_cert_chain(os.path.join(certdir, "server.pem"), os.path.join(certdir, "server.key"))
            tls_srv = await ws_serve(handler, "127.0.0.1", 0, ssl=ctx)
            ports["wss"] = tls_srv.sockets[0].getsockname()[1]
        tmp = ports_file + ".tmp"
        with open(tmp, "w") as f:
            json.dump(ports, f)
        os.rename(tmp, ports_file)
        await asyncio.sleep(120)


def main():
    check = sys.argv[1]
    if check == "serve":
        asyncio.run(serve_main(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None))
        return
    port = int(sys.argv[2])
    arg = sys.argv[3] if len(sys.argv) > 3 else None
    table = {
        "echo": lambda: asyncio.run(c_echo(port)),
        "fragments": lambda: asyncio.run(c_fragments(port)),
        "ping": lambda: asyncio.run(c_ping(port)),
        "close_client": lambda: asyncio.run(c_close_client(port)),
        "close_server": lambda: asyncio.run(c_close_server(port)),
        "stats": lambda: asyncio.run(c_stats(port, arg)),
        "many": lambda: asyncio.run(c_many(port, int(arg))),
        "plain": lambda: c_plain(port),
        "refusals": lambda: c_refusals(port),
        "bytewise": lambda: c_bytewise(port),
        "early": lambda: c_early(port),
        "violations": lambda: c_violations(port),
        "tls": lambda: c_tls(port, arg),
        "unwatched": lambda: c_unwatched(port),
        "quit": lambda: c_quit(port),
    }
    table[check]()


main()
