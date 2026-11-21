#!/usr/bin/env python3
"""`--profile=FILE.json`, read by Python's json module and held to what the
report must say (tests/run_profile.sh, TIER json). Prints `ok ...` or
`FAIL ...`; exits 1 on any failure.

  check_json.py sampled REPORT.json
      parses; the integers add up (every sample is one line's, waiting's or
      outside's); totals at least selfs; a builtin has no file, a function
      an absolute path that exists.
  check_json.py counts REPORT.json TEXT_REPORT
      counts only (sampled false, seconds null); every function's calls and
      every line's runs equal the text report's, row for row.
  check_json.py names REPORT.json WANT_SHOWN
      the program's file is named as WANT_SHOWN (a name with a quote, a
      control character and non-ASCII), and parses."""
import json
import os
import re
import sys

failed = 0


def check(cond, label, detail=""):
    global failed
    if cond:
        print("ok   " + label)
    else:
        failed += 1
        print("FAIL " + label + (": " + detail if detail else ""))


mode, path = sys.argv[1], sys.argv[2]
try:
    with open(path, "rb") as f:
        report = json.loads(f.read().decode("utf-8"))
except (ValueError, UnicodeDecodeError) as e:
    print("FAIL %s does not parse as JSON: %s" % (path, e))
    sys.exit(1)

functions = report.get("functions", [])
lines = report.get("lines", [])

if mode == "sampled":
    check(report.get("profile") == 1 and report.get("sampled") is True, "sampled, version 1")
    added = sum(l["self_samples"] for l in lines) + report["waiting_samples"] + report["outside_samples"]
    check(report["samples"] > 0 and added == report["samples"],
          "every sample is one line's, waiting's or outside's (%d of %d)" % (added, report["samples"]))
    bad = [f["name"] for f in functions if f["total_samples"] < f["self_samples"]]
    check(not bad, "every total is at least its self", ", ".join(bad))
    builtins = [f for f in functions if f["kind"] == "builtin"]
    check(builtins and all(f["file"] is None and f["path"] is None and f["line"] is None for f in builtins),
          "a builtin has no file (%d builtins)" % len(builtins))
    defined = [f for f in functions if f["kind"] != "builtin"]
    check(defined and all(os.path.isabs(f["path"]) and os.path.exists(f["path"]) for f in defined),
          "every function's path is absolute and exists")
    check(all(os.path.isabs(l["path"]) and os.path.exists(l["path"]) for l in lines),
          "every line's path is absolute and exists")
elif mode == "counts":
    text = open(sys.argv[3]).read().splitlines()
    check(report.get("sampled") is False and report.get("seconds") is None, "counts only: sampled false, no seconds")
    want_calls, want_runs, section = {}, {}, None
    for row in text:
        if row.startswith("functions"):
            section = "f"
        elif row.startswith("lines"):
            section = "l"
        elif row.startswith("  ") and section == "f":
            m = re.match(r"^  (\S+)( \[builtin\])? +(\d+)$", row)
            want_calls[m.group(1)] = int(m.group(3))
        elif row.startswith("  ") and section == "l":
            m = re.match(r"^  (\S+:\d+) +(\d+)", row)
            want_runs[m.group(1)] = int(m.group(2))
    got_calls = {f["name"]: f["calls"] for f in functions}
    got_runs = {"%s:%d" % (l["file"], l["line"]): l["runs"] for l in lines}
    check(got_calls == want_calls, "every function's calls as the text report (%d)" % len(got_calls),
          "%r against %r" % (got_calls, want_calls))
    check(got_runs == want_runs, "every line's runs as the text report (%d)" % len(got_runs),
          str(set(got_runs.items()) ^ set(want_runs.items())))
    lib = [f for f in functions if f["name"] == "countlib.triple"]
    check(lib and lib[0]["kind"] == "function" and lib[0]["path"].endswith("/tests/profile/countlib.bas"),
          "a library's function is a function, defined in its own file", repr(lib))
    back = [f for f in functions if f["name"] == "called_back"]
    check(back and back[0]["path"].endswith("/tests/profile/counts.bas"),
          "the program's function, first called from a library, is in the program's file", repr(back))
elif mode == "names":
    # A name that is not UTF-8 is written with each byte past ASCII as U+FFFD,
    # the whole name, so the file still parses.
    raw = os.fsencode(sys.argv[3])
    try:
        want = raw.decode("utf-8")
    except UnicodeDecodeError:
        want = "".join(chr(b) if b < 0x80 else "\ufffd" for b in raw)
    files = {l["file"] for l in lines}
    check(files == {want}, "the file is named as written: %r" % want, repr(files))
else:
    sys.exit("unknown mode " + mode)

sys.exit(1 if failed else 0)
