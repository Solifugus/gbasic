#!/usr/bin/env python3
"""The other end for tests/run_net.sh: a peer this module did not write.

    peer.py CHECK PORT [SERVER_PID]

Each check prints PASS or FAIL lines and exits non-zero on a failure.
"""
import hashlib
import os
import socket
import struct
import sys
import time


def fail(msg):
    print("FAIL " + msg)
    sys.exit(1)


def recv_until(s, n_lines, limit=10.0):
    data = b""
    end = time.time() + limit
    while data.count(b"\n") < n_lines:
        if time.time() > end:
            fail("timed out waiting for %d lines, got %r" % (n_lines, data[:200]))
        chunk = s.recv(65536)
        if not chunk:
            break
        data += chunk
    return data


def check_split(port):
    """A frame delivered ONE BYTE PER SEND reassembles exactly as one sent
    whole -- the defect class the take family exists for."""
    whole = socket.create_connection(("127.0.0.1", port))
    whole.sendall(b"alpha\nbeta\r\ngam\x00ma\n")
    a = recv_until(whole, 3)
    whole.close()
    dribble = socket.create_connection(("127.0.0.1", port))
    dribble.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    for b in b"alpha\nbeta\r\ngam\x00ma\n":
        dribble.sendall(bytes([b]))
        time.sleep(0.002)
    b = recv_until(dribble, 3)
    dribble.close()
    want = b"echo:alpha\necho:beta\necho:gam\x00ma\n"
    if a != want:
        fail("whole: %r" % a)
    if b != want:
        fail("byte at a time: %r" % b)
    print("PASS a line protocol reassembles identically sent whole and one byte per send, NUL included")


def check_frames(port):
    s = socket.create_connection(("127.0.0.1", port))
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    frames = [b"", b"x", bytes(range(256)), os.urandom(70000)]
    wire = b"".join(struct.pack(">I", len(f)) + f for f in frames)
    for i in range(0, len(wire), 7):          # cut at every 7th byte
        s.sendall(wire[i:i + 7])
    got = b""
    end = time.time() + 20
    while len(got) < len(wire) and time.time() < end:
        chunk = s.recv(65536)
        if not chunk:
            break
        got += chunk
    s.close()
    if got != wire:
        fail("frames came back different (%d of %d bytes)" % (len(got), len(wire)))
    print("PASS length-prefixed frames, cut at every 7th byte, echo back byte for byte (%d bytes)" % len(wire))


def check_many(port, n):
    socks = []
    for i in range(n):
        socks.append(socket.create_connection(("127.0.0.1", port)))
    for i, s in enumerate(socks):
        s.sendall(b"n%d\n" % i)
    for i, s in enumerate(socks):
        got = recv_until(s, 1)
        if got != b"echo:n%d\n" % i:
            fail("connection %d answered %r" % (i, got))
    for s in socks:
        s.close()
    print("PASS %d connections at once, each answered its own line" % n)


def check_flood(port):
    """A 10 MiB write against a slow reader with a small receive buffer: every
    byte arrives, in order. A partial send is the module's problem."""
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    s.connect(("127.0.0.1", port))
    want = b"0123456789abcdef" * 655360
    h = hashlib.sha256()
    total = 0
    reads = 0
    while True:
        chunk = s.recv(8192)
        if not chunk:
            break
        h.update(chunk)
        total += len(chunk)
        reads += 1
        if reads % 64 == 0:
            time.sleep(0.001)
    s.close()
    if total != len(want) or h.digest() != hashlib.sha256(want).digest():
        fail("received %d bytes, sha256 %s" % (total, h.hexdigest()))
    print("PASS a 10 MiB write reached a slow reader whole and in order")


def vm_hwm_kb(pid):
    with open("/proc/%d/status" % pid) as f:
        for line in f:
            if line.startswith("VmHWM:"):
                return int(line.split()[1])
    return -1


def check_hold(port, pid):
    """A fast sender against a program that never takes: reading pauses at the
    ceiling, so the server's peak memory stays flat however much is sent."""
    before = vm_hwm_kb(pid)
    s = socket.create_connection(("127.0.0.1", port))
    s.setblocking(False)
    sent = 0
    block = b"z" * 65536
    end = time.time() + 3
    while time.time() < end:
        try:
            sent += s.send(block)
        except BlockingIOError:
            time.sleep(0.01)
    after = vm_hwm_kb(pid)
    s.close()
    grew = after - before
    print("INFO offered %d bytes in 3 s; the server's peak memory grew %d KiB" % (sent, grew))
    if sent > 64 * 1024 * 1024:
        fail("the server accepted %d bytes, so reading never paused" % sent)
    if grew > 16 * 1024:
        fail("the server's peak memory grew %d KiB against a 64 KiB buffer" % grew)
    print("PASS a program that does not take pauses reading at its ceiling; memory stayed flat")


def check_quit(port):
    s = socket.create_connection(("127.0.0.1", port))
    s.sendall(b"quit\n")
    s.recv(10)
    s.close()


def main():
    check, port = sys.argv[1], int(sys.argv[2])
    if check == "split":
        check_split(port)
    elif check == "frames":
        check_frames(port)
    elif check == "many":
        check_many(port, int(sys.argv[3]))
    elif check == "flood":
        check_flood(port)
    elif check == "hold":
        check_hold(port, int(sys.argv[3]))
    elif check == "quit":
        check_quit(port)
    else:
        fail("unknown check " + check)


main()
