#!/usr/bin/env python3
"""Drive examples/steward/steward.bas the way three users would.

Prints one line per observation, which the runner asserts on. Deliberately
starts all three conversations BEFORE answering any approval, so the server is
holding two suspended runs while it serves other requests -- which is the
property the whole design exists for.
"""
import json
import os
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

port = int(sys.argv[1])
base = "http://127.0.0.1:%d" % port


def post(path, body, user=None):
    req = urllib.request.Request(base + path, data=body.encode(), method="POST")
    if user:
        req.add_header("X-User", user)
    try:
        with urllib.request.urlopen(req, timeout=10) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()


def get(path):
    try:
        with urllib.request.urlopen(base + path, timeout=10) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()


# All three conversations start before any approval is answered.
_, a = post("/ask", "what is the balance for a1?", "alice")
_, b = post("/ask", "freeze card c9", "alice")
_, c = post("/ask", "freeze card c9", "bob")
time.sleep(2.5)

print("parked_b: %s" % get("/run/" + b)[1].split("|")[0])
print("parked_c: %s" % get("/run/" + c)[1].split("|")[0])
print("early_a: %s" % get("/run/" + a)[1].split("|")[0])

post("/approve/" + b, "yes")
post("/approve/" + c, "no")
time.sleep(3)

print("a: %s" % get("/run/" + a)[1])
print("b: %s" % get("/run/" + b)[1])
print("c: %s" % get("/run/" + c)[1])
print("no_identity: %d" % post("/ask", "hello")[0])
print("empty_identity: %d" % post("/ask", "hello", "")[0])
print("unknown_run: %d" % get("/run/nope")[0])
print("approve_unknown: %d" % post("/approve/nope", "yes")[0])
