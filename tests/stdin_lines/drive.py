#!/usr/bin/env python3
"""Drives tests/stdin_lines fixtures from outside, and is their oracle.

    drive.py pipe GBASIC        lines of every kind, in one write and in pieces
    drive.py slow GBASIC        a line every 0.4 s: each echo must come back
                                before the next is written, not at the end
    drive.py qt GBASIC          the Qt fixture: set, ticks, set, quit
Prints `checks: N` and `mismatches: M`; exits nonzero on any mismatch.
"""
import os
import random
import subprocess
import sys
import time

checks = bad = 0


def check(label, ok, detail=""):
    global checks, bad
    checks += 1
    if not ok:
        bad += 1
        print("MISMATCH %s %s" % (label, detail))


def expect_echo(lines):
    return ["%d %s" % (len(l), l.hex()) for l in lines]


def pipe(gb):
    r = random.Random(7)
    lines = [b"plain", b"", b"tab\there", "Łódź".encode(), b"\xff\x00bytes",
             b"x" * 70000]
    for _ in range(3000):
        lines.append(bytes(r.choice(b"abc xyz019") for _ in range(r.randint(0, 40))))
    data = b"".join(l + (b"\r\n" if i % 7 == 3 else b"\n") for i, l in enumerate(lines))
    data += b"no newline at the end"
    lines.append(b"no newline at the end")
    out = subprocess.run([gb, "--line-buffered", "tests/stdin_lines/echo.bas"], input=data,
                         capture_output=True, timeout=60)
    got = out.stdout.decode("ascii").splitlines()
    check("exit 0", out.returncode == 0, str(out.returncode))
    check("stderr empty", out.stderr == b"", out.stderr[:200])
    check("every line back, in order, CR removed, the last unterminated one too",
          got[:-1] == expect_echo(lines), "%d lines back, %d sent" % (len(got) - 1, len(lines)))
    check("closed, and every delivery held one line",
          got[-1:] == ["closed; at most 1 line per delivery"], got[-1:])


def slow(gb):
    p = subprocess.Popen([gb, "--line-buffered", "tests/stdin_lines/echo.bas"],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    lags = []
    for i in range(5):
        line = b"line %d" % i
        p.stdin.write(line + b"\n")
        p.stdin.flush()
        t0 = time.monotonic()
        back = p.stdout.readline().decode().strip()
        lags.append(time.monotonic() - t0)
        check("line %d echoed while the input is still open" % i, back == expect_echo([line])[0], back)
        time.sleep(0.4)
    p.stdin.close()
    tail = p.stdout.readline().decode().strip()
    check("closed when the input closes", tail == "closed; at most 1 line per delivery", tail)
    check("the program ends", p.wait(timeout=10) == 0)
    check("each line came back in under a second", max(lags) < 1.0, "%.2f s worst" % max(lags))


def qt(gb):
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    p = subprocess.Popen([gb, "--line-buffered", "tests/stdin_lines/qt_drive.bas"], env=env,
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    def say(line):
        p.stdin.write(line.encode() + b"\n")
        p.stdin.flush()
    say("set hello")
    first = p.stdout.readline().decode().strip()
    if first == "NODISPLAY":
        print("SKIP no display")
        p.kill()
        return
    check("a line sets the label while qt.run holds the thread", first == "shown hello", first)
    time.sleep(1.5)
    say("ticks")
    ticks = p.stdout.readline().decode().strip()
    n = int(ticks.split()[1]) if ticks.startswith("ticks ") else -1
    check("the timer kept firing while a line was awaited (1.5 s, every 0.1 s)", n >= 8, ticks)
    say("set Łódź")
    check("a second line, later", p.stdout.readline().decode().strip() == "shown Łódź")
    say("quit")
    check("quit stops Qt's loop and qt.run returns", p.stdout.readline().decode().strip() == "run returned")
    p.stdin.close()
    check("the program ends", p.wait(timeout=10) == 0)
    err = b"\n".join(l for l in p.stderr.read().splitlines() if b"propagateSizeHints" not in l)
    check("stderr empty", err == b"", err[:200])


{"pipe": pipe, "slow": slow, "qt": qt}[sys.argv[1]](sys.argv[2])
print("checks: %d\nmismatches: %d" % (checks, bad))
sys.exit(1 if bad else 0)
