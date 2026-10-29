#!/usr/bin/env python3
"""Step 4 of docs/gsql_storage_design.md §6: score the candidates under the
weights fixed on 2026-10-24.

    python3 research/gsql/score.py [--page8k] [--rep N] [--space-at-scale] [--recovery-floor MS]

The last two are SENSITIVITY variants, not the rule: space measured at 10^7
rows instead of W7's 10^5, and recovery times under MS counted as MS.

Reads the 2026-10-28 round (results/2026-10-28/round-*.jsonl), the handoff
lines (results-gdash.jsonl) and counts lines of code in the sources. Prints
markdown tables. Only the three candidates are scored; within a criterion each
scores the geometric mean, over that criterion's workloads, of its result over
the best candidate's (inverted where lower is better), and a workload it could
not run scores zero.

The rule for one workload, fixed before the round's numbers were seen: the
throughput ratio, and where a p99 is reported the geometric mean of that and
the inverted p99 ratio (the worst p99 of any operation kind in the line).
"""
import json, math, re, sys, os

HERE = os.path.dirname(os.path.abspath(__file__))
R = os.environ.get("GSQL_ROUND_DIR") or os.path.join(HERE, "results", "2026-10-28")
CANDS = ["cow", "wal", "lsm"]
NAMES = {"cow": "2.A", "wal": "2.B", "lsm": "2.C"}
WEIGHTS = {"standard": 35, "specific": 20, "complexity": 20, "space": 15, "recovery": 10}


def lines(path):
    out = []
    if not os.path.exists(path):
        return out
    for l in open(path):
        try:
            out.append(json.loads(l))
        except ValueError:
            pass
    return out


def worst_p99(r):
    p = [v["p99_us"] for v in r.values() if isinstance(v, dict) and "p99_us" in v and v.get("n", 1)]
    return max(p) if p else None


def index(files, page=4096):
    by = {}
    for f in files:
        for r in lines(os.path.join(R, f)):
            if r.get("page", 4096) != page or "failed" in r or r.get("errors"):
                continue
            by[(r["engine"], r["workload"])] = r
    return by


FLOOR = None  # --recovery-floor MS: SENSITIVITY, a time at or under MS counts as MS


def ratio_score(vals, higher_better, floor=None):
    """vals: engine -> value or None. Returns engine -> ratio to the best."""
    if floor is not None:
        vals = {e: (None if v is None else max(v, floor)) for e, v in vals.items()}
    ok = {e: v for e, v in vals.items() if v is not None and v > 0}
    if not ok:
        return {e: None for e in vals}
    best = max(ok.values()) if higher_better else min(ok.values())
    return {e: (0.0 if e not in ok else (ok[e] / best if higher_better else best / ok[e])) for e in vals}


def geo(xs):
    xs = [x for x in xs if x is not None]
    if not xs:
        return None
    if any(x == 0 for x in xs):
        return 0.0
    return math.exp(sum(math.log(x) for x in xs) / len(xs))


def workload_terms(by, wl, cands, thr_key="ops_s"):
    """engine -> score for one workload: throughput, with p99 where reported."""
    thr = {e: by.get((e, wl), {}).get(thr_key, by.get((e, wl), {}).get("load_ops_s")) for e in cands}
    p99 = {e: (worst_p99(by[(e, wl)]) if (e, wl) in by else None) for e in cands}
    t = ratio_score(thr, True)
    if all(p99[e] is not None for e in cands if (e, wl) in by) and any(p99.values()):
        p = ratio_score(p99, False)
        return {e: (None if t[e] is None else (0.0 if t[e] == 0 else geo([t[e], p[e]]))) for e in cands}, thr, p99
    return t, thr, p99


def loc_of(files):
    n = 0
    for f in files:
        text = open(os.path.join(HERE, f)).read()
        text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
        n += sum(1 for l in text.split("\n") if re.sub(r"//.*", "", l).strip())
    return n


def main():
    global FLOOR
    page = 8192 if "--page8k" in sys.argv else 4096
    if "--recovery-floor" in sys.argv:
        FLOOR = float(sys.argv[sys.argv.index("--recovery-floor") + 1])
    # --rep N reads repetition N of everything but W4, which was run once (rep 1 is the round)
    rep = sys.argv[sys.argv.index("--rep") + 1] if "--rep" in sys.argv else "1"
    pre = "round-" if rep == "1" else f"rep{rep}-"
    files = [pre + "main.jsonl", pre + "tpcc.jsonl", pre + "ssb.jsonl", pre + "w3.jsonl", "round-w4.jsonl", "round-w4-carried.jsonl"]
    by = index(files)
    if page == 8192:
        # the B+trees at 8 KiB where the round ran them; W3 and W4 were run at 4 KiB only
        by8 = index([pre + "page8k.jsonl"], 8192)
        for k, v in by8.items():
            by[k] = v
    out = []
    crit = {}

    def section(title, rows, cname):
        out.append(f"\n**{title}**\n")
        out.append("| workload | " + " | ".join(NAMES[e] for e in CANDS) + " |")
        out.append("|---|" + "---|" * len(CANDS))
        per = {e: [] for e in CANDS}
        for label, sc in rows:
            out.append(f"| {label} | " + " | ".join("—" if sc[e] is None else f"{sc[e]:.2f}" for e in CANDS) + " |")
            for e in CANDS:
                if sc[e] is not None:
                    per[e].append(sc[e])
        crit[cname] = {e: geo(per[e]) for e in CANDS}
        out.append("| **criterion** | " + " | ".join(f"**{crit[cname][e]:.3f}**" for e in CANDS) + " |")

    std = []
    for wl in ["ycsb-load", "ycsb-a", "ycsb-b", "ycsb-c", "ycsb-d", "ycsb-e", "ycsb-f", "tpcc"]:
        std.append((wl, workload_terms(by, wl, CANDS)[0]))
    for q in ["q1_1", "q1_2", "q1_3"]:
        v = {e: by.get((e, "ssb1"), {}).get(q, {}).get("median_ms") for e in CANDS}
        std.append((f"ssb {q.replace('_', '.')} (ms)", ratio_score(v, False)))
    section("Standard workloads (35)", std, "standard")

    spec = []
    for wl in ["w1-uniform", "w2", "w3", "w5-100", "w6", "w7", "w8", "w9", "group-8", "group-64"]:
        spec.append((wl, workload_terms(by, wl, CANDS)[0]))
    for wl in ["w4-sorted", "w4-random"]:
        v = {e: by.get((e, wl), {}).get("load_ops_s") for e in CANDS}
        spec.append((wl + " (10⁷ rows)", ratio_score(v, True)))
    section("Engine-specific workloads (20)", spec, "specific")

    src = {"cow": ["cow.c", "btree.c", "btengine.c"], "wal": ["wal.c", "btree.c", "btengine.c"], "lsm": ["lsm.c"]}
    loc = {e: loc_of(src[e]) for e in CANDS}
    section("Implementation complexity (20): lines of code, comments and blanks excluded",
            [("lines (" + ", ".join(f"{NAMES[e]} {loc[e]:,}" for e in CANDS) + ")", ratio_score(loc, False))],
            "complexity")

    if "--space-at-scale" in sys.argv:
        # SENSITIVITY, not the rule: space at 10^7 rows, where a fixed log reservation is noise
        v = {e: by.get((e, "w4-random"), {}).get("space_amp") for e in CANDS}
        section("Space amplification (15), SENSITIVITY: W4 random at 10⁷ rows",
                [("w4-random space amp (" + ", ".join(f"{NAMES[e]} {v[e]}" for e in CANDS) + ")", ratio_score(v, False))], "space")
    else:
        v = {e: by.get((e, "w7"), {}).get("space_amp") for e in CANDS}
        section("Space amplification (15): W7, as the weights table names it",
                [("w7 space amp (" + ", ".join(f"{NAMES[e]} {v[e]}" for e in CANDS) + ")", ratio_score(v, False))], "space")

    rec = []
    v = {e: by.get((e, "recovery"), {}).get("recover_ms") for e in CANDS}
    rec.append(("reopen after a crash (bench, ms)", ratio_score(v, False, FLOOR)))
    hand = [r for r in lines(os.path.join(R, "results-gdash.jsonl")) if r.get("test") == "handoff" and r.get("mode") == "sigkill"]
    for c in sorted({r["commits"] for r in hand}):
        v = {e: next((r["to_first_read_ms"] for r in hand if r["engine"] == e and r["commits"] == c), None) for e in CANDS}
        rec.append((f"handoff by SIGKILL after {c:,} commits (ms)", ratio_score(v, False, FLOOR)))
    section("Recovery and handoff time (10)" + (f", SENSITIVITY: floored at {FLOOR:g} ms" if FLOOR else ""), rec, "recovery")

    out.append("\n**Weighted total (out of 100)**\n")
    out.append("| | " + " | ".join(NAMES[e] for e in CANDS) + " |")
    out.append("|---|" + "---|" * len(CANDS))
    tot = {e: 0.0 for e in CANDS}
    for c, w in WEIGHTS.items():
        out.append(f"| {c} ({w}) | " + " | ".join(f"{w * crit[c][e]:.1f}" for e in CANDS) + " |")
        for e in CANDS:
            tot[e] += w * crit[c][e]
    out.append("| **total** | " + " | ".join(f"**{tot[e]:.1f}**" for e in CANDS) + " |")
    print(f"page size for the B+trees: {page}")
    print("\n".join(out))
    if FLOOR is None and "--space-at-scale" not in sys.argv:
        json.dump({"page": page, "rep": rep, "criteria": crit, "total": tot}, open(os.path.join(R, f"score-{page}-rep{rep}.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
