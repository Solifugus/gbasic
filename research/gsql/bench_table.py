#!/usr/bin/env python3
"""Summarise bench.sh results (one JSON object per line) as a table per workload."""
import json
import sys

rows = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
by_wl = {}
for r in rows:
    by_wl.setdefault(r["workload"] + (f" (page {r['page']})" if r.get("page", 4096) != 4096 else ""), []).append(r)


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
        if wl.startswith("tpcc"):
            # TPC-C-like: no think times, so a rate here is NOT a tpmC
            no, pay = r["new_order"], r["payment"]
            print(f"  {r['engine']:7} {r['ops_s']:>7,} tx/s  new-order/min {r['new_order_per_min']:>9,}  "
                  f"new-order p50/p99 {no['p50_us']:>7,.0f}/{no['p99_us']:>7,.0f} us  payment p50 {pay['p50_us']:>6,.0f} us  "
                  f"w-amp {r['write_amp']:>6.2f}  space {r['space_amp']:.2f}  rss {r['peak_rss_kb'] / 1024:.0f} MB  errors {r['errors']}")
            continue
        if wl.startswith("ssb1"):
            qs = "  ".join(f"Q1.{i} {r[f'q1_{i}']['median_ms']:>8,.1f} ms" for i in (1, 2, 3))
            print(f"  {r['engine']:7} {r['rows']:,} rows  load {r['load_rows_s']:>9,} rows/s  {qs}  "
                  f"scan {r['q1_1']['rows_s'] / 1e6:>5.1f} M rows/s  space {r['space_amp']:.2f}  answers {'ok' if r['answers_ok'] else 'WRONG'}")
            continue
        if wl.startswith("recovery"):
            # No measured ops: the child crashed after its updates, and what
            # is timed is the reopen plus a first get.
            print(f"  {r['engine']:7} reopen {r['recover_ms']:,.2f} ms   file growth {r.get('file_growth', 0):.2f}")
            continue
        n = r.get("ops") or r.get("records")
        thr = r.get("ops_s", r.get("load_ops_s"))
        fl = r.get("flushes")
        fpo = (fl / n) if (fl is not None and n) else None
        print(f"  {r['engine']:7} {fmt(thr):>9} {fmt(lat(r, 'write', 'p50_us'), 0):>10} {fmt(lat(r, 'write', 'p99_us'), 0):>10} "
              f"{fmt(lat(r, 'read', 'p50_us'), 1):>9} {fmt(lat(r, 'read', 'p99_us'), 1):>9} {fmt(lat(r, 'scan', 'p50_us'), 0):>9} "
              f"{r['write_amp']:>6.2f} {r['space_amp']:>6.2f} {fmt(fpo, 2):>8} {r['peak_rss_kb'] / 1024:>7.0f}")
