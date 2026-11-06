#!/usr/bin/env python3
"""Send PACKETS length-prefixed 32 KiB packets to 127.0.0.1:PORT as fast as
the socket takes them: a peer faster than a waited gBASIC reader, so the
reader's buffer sits full (run_net.sh tier 10)."""
import os
import socket
import struct
import sys

c = socket.create_connection(("127.0.0.1", int(sys.argv[1])))
packet = struct.pack(">I", 32768) + os.urandom(32768)
for _ in range(int(sys.argv[2])):
    c.sendall(packet)
c.close()
