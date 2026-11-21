#!/usr/bin/env python3
"""The signal paths of `gbasic --profile` (docs/profiler_design.md §5), for
tests/run_profile.sh's TIER signal. Python reads the wait status, which a
shell cannot tell apart from an exit of 128+N (as run_lock_signal.sh).

Each case waits for the program's `ready` line, so nothing depends on timing
except the grace case, which measures it. Prints `ok ...` or `FAIL ...`."""
import os
import signal
import subprocess
import sys
import time

GBASIC = sys.argv[1]
SCALE = float(os.environ.get("GATE_TIME_SCALE", "1") or "1")
failed = 0


def check(cond, label, detail=""):
    global failed
    if cond:
        print("ok   " + label)
    else:
        failed += 1
        print("FAIL " + label + (": " + detail if detail else ""))


def default_signals():
    """Each child starts with the three signals at their defaults. The gate runs
    suites as background jobs, and bash starts those with SIGINT IGNORED,
    which a child inherits -- and the profiler rightly leaves an ignored
    signal alone, so a Ctrl-C case would never end (found by the gate)."""
    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, signal.SIG_DFL)


def start(prog, env_extra=None, ignore=None, stdin=subprocess.DEVNULL):
    env = dict(os.environ)
    env.update(env_extra or {})

    def pre():
        default_signals()
        if ignore:
            signal.signal(ignore, signal.SIG_IGN)

    p = subprocess.Popen([GBASIC, "--line-buffered", "--profile", prog], stdin=stdin,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, preexec_fn=pre)
    line = p.stdout.readline().decode()
    return p, line


def finish(p, timeout=30):
    try:
        out, err = p.communicate(timeout=timeout * SCALE)
    except subprocess.TimeoutExpired:
        p.kill()
        out, err = p.communicate()
        return None, out.decode(), err.decode()
    return p.returncode, out.decode(), err.decode()


def report_line(err):
    return next((l for l in err.splitlines() if l.startswith("profile:")), "")


# Waiting: SIGTERM during sleep. The sampler writes the report and the process
# dies by SIGTERM, as it would have unprofiled.
p, ready = start("tests/profile/ready_sleep.bas")
p.send_signal(signal.SIGTERM)
rc, out, err = finish(p)
check(ready == "ready\n" and rc == -signal.SIGTERM, "asleep, SIGTERM: dies by SIGTERM", "rc %s" % rc)
check(report_line(err).startswith("profile: stopped by SIGTERM after "), "asleep: the report says so",
      report_line(err) or err[:200])
check("(waiting)" in err, "asleep: the time is the waiting row's")

# Running: SIGINT (a Ctrl-C) while computing. The main thread meets it at the
# next statement and writes the report itself.
p, ready = start("tests/profile/ready_spin.bas")
time.sleep(0.2)
p.send_signal(signal.SIGINT)
rc, out, err = finish(p)
check(rc == -signal.SIGINT, "computing, SIGINT: dies by SIGINT", "rc %s" % rc)
check(report_line(err).startswith("profile: stopped by SIGINT after "), "computing: the report says so",
      report_line(err) or err[:200])
check(any(l.rstrip().endswith("s = s + 1") and int(l.split()[1]) > 0 for l in err.splitlines()),
      "computing: the loop's line has its runs")

# Inside one C call: a read of a pipe held open never reaches a statement, so
# after its grace (200 ms) the sampler writes the report and ends the run.
# The driver keeps the pipe's write end: communicate() would close it, and the
# read would return at its end.
held_r, held_w = os.pipe()
p, ready = start("tests/profile/ready_read.bas", stdin=held_r)
t = time.monotonic()
p.send_signal(signal.SIGTERM)
rc, out, err = finish(p)
took = time.monotonic() - t
check(rc == -signal.SIGTERM, "blocked in a read, SIGTERM: dies by SIGTERM", "rc %s" % rc)
check(report_line(err).startswith("profile: stopped by SIGTERM"), "blocked in a read: the report is written",
      err[:200])
check(0.15 <= took <= 10 * SCALE, "blocked in a read: after the grace (%.3f s)" % took)

# A second signal ends it at once, as the default would: no waiting out the
# grace, so no report. 50 ms apart, well inside the grace: two sent together
# are one pending signal.
p, ready = start("tests/profile/ready_read.bas", stdin=held_r)
t = time.monotonic()
p.send_signal(signal.SIGTERM)
time.sleep(0.05)
p.send_signal(signal.SIGTERM)
rc, out, err = finish(p)
took = time.monotonic() - t
os.close(held_r)
os.close(held_w)
check(rc == -signal.SIGTERM and "profile:" not in err,
      "a second SIGTERM ends it at once (%.3f s), with no report" % took, "rc %s, %r" % (rc, err[:120]))

# A signal the program was started ignoring stays ignored (nohup).
p, ready = start("tests/profile/ready_sleep.bas", ignore=signal.SIGTERM)
p.send_signal(signal.SIGTERM)
time.sleep(0.5)
alive = p.poll() is None
p.kill()
rc, out, err = finish(p)
check(alive and "profile:" not in err, "an ignored SIGTERM stays ignored")

# Counts only (GBASIC_PROFILE_HZ=0) has no sampler, and a signal does what it
# did: a main thread blocked in a read would never come back to act on it.
p, ready = start("tests/profile/ready_sleep.bas", env_extra={"GBASIC_PROFILE_HZ": "0"})
p.send_signal(signal.SIGTERM)
rc, out, err = finish(p)
check(rc == -signal.SIGTERM and "profile:" not in err, "counts only: SIGTERM kills, with no report",
      "rc %s, %r" % (rc, err[:120]))

# A server owns SIGTERM (it drains): that stays the server's, and the report
# follows the drain's normal end.
p = subprocess.Popen([GBASIC, "--line-buffered", "--profile", "tests/web_server_block/block_drain.bas"],
                     stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                     preexec_fn=default_signals)
port = p.stdout.readline().decode()
p.send_signal(signal.SIGTERM)
rc, out, err = finish(p)
hook = err.find("hook: draining now")
rep = err.find("profile: ")
check(port.startswith("PORT ") and rc == 0, "a server: SIGTERM still drains, exit 0", "rc %s, %r" % (rc, err[:200]))
check(0 <= hook < rep and not report_line(err).startswith("profile: stopped"),
      "a server: the report follows the drain", err[:300])

sys.exit(1 if failed else 0)
