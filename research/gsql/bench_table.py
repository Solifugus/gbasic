#!/usr/bin/env python3
"""Summarise bench.sh results (one JSON object per line) as a table per workload."""
import json
import sys

rows = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
by_wl = {}
for r in rows:
    by_wl.setdefault(r["workload"], []).append(r)


def lat(r, k, f):
    return r[k][f] if k in r else None


def fmt(x, nd=0):
    if x is None:
        return "-"
    if isinstance(x, float):
        return f"{x:,.{nd}f}"
    return f"{x:,}"


for wl, rs in by_wl.items():
    print(f"\n{wl}")
    print(f"  {'engine':7} {'ops/s':>9} {'write p50':>10} {'write p99':>10} {'read p50':>9} {'read p99':>9} "
          f"{'scan p50':>9} {'w-amp':>6} {'space':>6} {'flush/op':>8} {'rss MB':>7}")
    for r in rs:
        if "failed" in r:
            print(f"  {r['engine']:7} FAILED ({r['failed']})")
            continue
        n = r.get("ops") or r.get("records")
        thr = r.get("ops_s", r.get("load_ops_s"))
        fl = r.get("flushes")
        fpo = (fl / n) if (fl is not None and n) else None
        print(f"  {r['engine']:7} {fmt(thr):>9} {fmt(lat(r, 'write', 'p50_us'), 0):>10} {fmt(lat(r, 'write', 'p99_us'), 0):>10} "
              f"{fmt(lat(r, 'read', 'p50_us'), 1):>9} {fmt(lat(r, 'read', 'p99_us'), 1):>9} {fmt(lat(r, 'scan', 'p50_us'), 0):>9} "
              f"{r['write_amp']:>6.2f} {r['space_amp']:>6.2f} {fmt(fpo, 2):>8} {r['peak_rss_kb'] / 1024:>7.0f}")
