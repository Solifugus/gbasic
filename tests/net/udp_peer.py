"""Python's socket module as the other end of gBASIC's datagram sockets (N3).

    udp_peer.py echo READY_FILE        an echo server; writes its port, runs 120 s
    udp_peer.py CHECK PORT [PORT2]     drive tests/net/datagram_server.bas

Checks, each printing PASS or FAIL lines:
    lockstep  1000 datagrams of random sizes (0..1400) and random bytes, each
              awaiting its echo: every one comes back byte for byte
    burst     100 datagrams sent without waiting, sizes 1..100: 100 echoes,
              each exactly one of the sizes sent, so none was split or joined
    big       the largest IPv4 datagram (65507 bytes) echoed whole
    trunc     100 bytes to PORT2 (max_datagram 16): the server reports
              truncated, the sent size and the 16 bytes it kept
    quit      tells the server to close its sockets and exit
"""
import os
import random
import socket
import sys
import time


def fail(msg):
    print("FAIL " + msg)
    sys.exit(1)


def client():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    s.settimeout(5)
    return s


def echo(ready):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    tmp = ready + ".tmp"
    with open(tmp, "w") as f:
        f.write(str(s.getsockname()[1]))
    os.rename(tmp, ready)
    s.settimeout(1)
    end = time.time() + 120
    while time.time() < end:
        try:
            data, addr = s.recvfrom(70000)
        except socket.timeout:
            continue
        if data == b"quit":
            return
        s.sendto(data, addr)


def lockstep(port):
    s = client()
    rng = random.Random(1234)
    for i in range(1000):
        n = rng.randint(0, 1400)
        data = bytes(rng.randrange(256) for _ in range(n))
        s.sendto(data, ("127.0.0.1", port))
        got, addr = s.recvfrom(70000)
        if got != data:
            fail("lockstep: datagram %d (%d bytes) came back as %d different bytes" % (i, n, len(got)))
        if addr[1] != port:
            fail("lockstep: the echo came from port %d, not %d" % (addr[1], port))
    print("PASS lockstep: 1000 datagrams of random size and bytes echoed byte for byte")


def burst(port):
    s = client()
    sent = [bytes([65 + i % 26]) * i for i in range(1, 101)]
    for d in sent:
        s.sendto(d, ("127.0.0.1", port))
    got = []
    try:
        while len(got) < len(sent):
            got.append(s.recvfrom(70000)[0])
    except socket.timeout:
        fail("burst: %d of %d echoes came back (a datagram lost on loopback, or joined)" % (len(got), len(sent)))
    if sorted(got, key=len) != sent:
        fail("burst: the echoes are not the datagrams sent: sizes %s" % sorted(len(g) for g in got)[:20])
    print("PASS burst: 100 datagrams sent at once came back as 100, none split or joined")


def big(port):
    s = client()
    data = bytes(range(256)) * 255 + bytes(range(227))
    assert len(data) == 65507
    s.sendto(data, ("127.0.0.1", port))
    got = s.recvfrom(70000)[0]
    if got != data:
        fail("big: 65507 bytes came back as %d" % len(got))
    print("PASS big: the largest IPv4 datagram (65507 bytes) echoed whole")


def trunc(port):
    s = client()
    data = b"0123456789abcdef" + b"X" * 84
    s.sendto(data, ("127.0.0.1", port))
    got = s.recvfrom(70000)[0]
    want = b"truncated:true size:100 kept:0123456789abcdef"
    if got != want:
        fail("trunc: the server reported %r, want %r" % (got, want))
    print("PASS trunc: a datagram past max_datagram was reported truncated, with its size, never silently cut")


def quit_(port):
    s = client()
    s.sendto(b"quit", ("127.0.0.1", port))
    print("INFO quit sent")


def main():
    check = sys.argv[1]
    if check == "echo":
        echo(sys.argv[2])
        return
    port = int(sys.argv[2])
    if check == "trunc":
        trunc(int(sys.argv[3]))
        return
    {"lockstep": lockstep, "burst": burst, "big": big, "quit": quit_}[check](port)


main()
