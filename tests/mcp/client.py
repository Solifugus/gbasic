#!/usr/bin/env python3
"""Drive tests/mcp/server.bas over stdio, the way a desktop MCP client does.

Usage: client.py [--line-buffered|none]

The flag is the point of the whole exercise: without `--line-buffered` the
server's replies sit in a block buffer while the client waits for them and the
server waits for the client. That is a deadlock, and this script reports
TIMEOUT for each request rather than hanging, so the runner can assert the
difference between the two invocations.
"""
import json
import os
import queue
import subprocess
import sys
import threading

flag = sys.argv[1] if len(sys.argv) > 1 else "--line-buffered"
# How long to wait for a reply. The deadlock probe passes a short one on
# purpose: establishing that nothing arrives needs patience per request, and
# four requests at six seconds each makes a tier slow enough to hit its own
# outer bound on a busy machine.
WAIT = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0
cmd = ["./gbasic"] + ([] if flag == "none" else [flag]) + ["tests/mcp/server.bas"]
env = dict(os.environ)
env["GBASIC_PATH"] = "stdlib"
env["MCP_PRINCIPAL"] = "alice"

# BYTES, not text mode, and a READER THREAD rather than select(): both so the
# same client runs on Windows. There select() accepts only sockets (a pipe
# raises WinError 10093), and text mode writes "\n" as "\r\n" -- so the blank
# line that ends a session arrived as "\r", which is not blank.
proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.PIPE, env=env)
lines = queue.Queue()


def pump():
    for raw in iter(proc.stdout.readline, b""):
        lines.put(raw.decode("utf-8"))
    lines.put(None)                     # EOF


threading.Thread(target=pump, daemon=True).start()


def rpc(obj, timeout=None):
    timeout = WAIT if timeout is None else timeout
    proc.stdin.write((json.dumps(obj) + "\n").encode("utf-8"))
    proc.stdin.flush()
    try:
        line = lines.get(timeout=timeout)
    except queue.Empty:
        return "TIMEOUT"
    if line is None:
        return "EOF"
    return json.loads(line)


def show(label, value):
    print("%s: %s" % (label, "TIMEOUT" if value in ("TIMEOUT", "EOF") else json.dumps(value)))


init = rpc({"jsonrpc": "2.0", "id": 1, "method": "initialize"})
show("initialize", init)
listed = rpc({"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
if listed in ("TIMEOUT", "EOF"):
    print("tools: TIMEOUT")
else:
    print("tools: %s" % ",".join(t["name"] for t in listed["result"]["tools"]))
called = rpc({"jsonrpc": "2.0", "id": 3, "method": "tools/call",
              "params": {"name": "get_balance", "arguments": {"id": "a1"}}})
show("call", called)
who = rpc({"jsonrpc": "2.0", "id": 4, "method": "tools/call",
           "params": {"name": "whoami", "arguments": {}}})
show("principal", who)

try:
    proc.stdin.write(b"\n")
    proc.stdin.flush()
    proc.wait(timeout=5)
except Exception:
    proc.kill()
print("exit: %s" % proc.returncode)
