#!/usr/bin/env python3
"""The analytics tasks in pandas and polars, written as a user of each would.

usage: py_bench.py pandas|polars ORDERS.csv CUSTOMERS.csv
Prints one line per task: `task<TAB>seconds<TAB>answer`, the seconds the best
of three runs inside this process, and `load` for reading both files.

  filter   region West and qty >= 10: count and sum of qty*price
  group    per region: count and sum of qty*price
  bycust   per customer: sum of qty*price, the top 5 (ties by id)
  top      the 10 largest orders by qty*price (ties by id)
  join     orders joined to customers: sum of qty*price per segment
  month    orders per month of day

Sums are printed to the cent, so a float total and an exact one compare.
"""
import sys
import time


def best(fn, runs=3):
    t = None
    out = None
    for _ in range(runs):
        t0 = time.perf_counter()
        out = fn()
        dt = time.perf_counter() - t0
        t = dt if t is None else min(t, dt)
    return t, out


def cents(x):
    return f"{x:.2f}"


def run_pandas(op, cp):
    import pandas as pd
    t, (o, c) = best(lambda: (pd.read_csv(op, parse_dates=["day"]), pd.read_csv(cp)), 1)
    yield "load", t, f"{len(o)} rows"
    o["amount"] = o["qty"] * o["price"]

    def f_filter():
        m = o[(o["region"] == "West") & (o["qty"] >= 10)]
        return f"{len(m)} {cents(m['amount'].sum())}"

    def f_group():
        g = o.groupby("region")["amount"].agg(["count", "sum"]).sort_index()
        return " ".join(f"{k}:{int(r['count'])}:{cents(r['sum'])}" for k, r in g.iterrows())

    def f_bycust():
        g = o.groupby("customer")["amount"].sum().reset_index()
        g = g.sort_values(["amount", "customer"], ascending=[False, True]).head(5)
        return " ".join(f"{int(r.customer)}:{cents(r.amount)}" for r in g.itertuples())

    def f_top():
        t = o.sort_values(["amount", "id"], ascending=[False, True]).head(10)
        return " ".join(str(i) for i in t["id"])

    def f_join():
        j = o.merge(c, left_on="customer", right_on="id", suffixes=("", "_c"))
        g = j.groupby("segment")["amount"].sum().sort_index()
        return " ".join(f"{k}:{cents(v)}" for k, v in g.items())

    def f_month():
        g = o.groupby(o["day"].dt.strftime("%Y-%m")).size().sort_index()
        return f"{len(g)} {g.iloc[0]} {g.iloc[-1]}"

    for name, fn in [("filter", f_filter), ("group", f_group), ("bycust", f_bycust),
                     ("top", f_top), ("join", f_join), ("month", f_month)]:
        t, ans = best(fn)
        yield name, t, ans


def run_polars(op, cp):
    import polars as pl
    t, (o, c) = best(lambda: (pl.read_csv(op, try_parse_dates=True), pl.read_csv(cp)), 1)
    yield "load", t, f"{o.height} rows"
    o = o.with_columns((pl.col("qty") * pl.col("price")).alias("amount"))

    def f_filter():
        m = o.filter((pl.col("region") == "West") & (pl.col("qty") >= 10))
        return f"{m.height} {cents(m['amount'].sum())}"

    def f_group():
        g = o.group_by("region").agg(pl.len().alias("n"), pl.col("amount").sum()).sort("region")
        return " ".join(f"{r[0]}:{r[1]}:{cents(r[2])}" for r in g.iter_rows())

    def f_bycust():
        g = o.group_by("customer").agg(pl.col("amount").sum())
        g = g.sort(["amount", "customer"], descending=[True, False]).head(5)
        return " ".join(f"{r[0]}:{cents(r[1])}" for r in g.iter_rows())

    def f_top():
        t = o.sort(["amount", "id"], descending=[True, False]).head(10)
        return " ".join(str(i) for i in t["id"])

    def f_join():
        j = o.join(c, left_on="customer", right_on="id", suffix="_c")
        g = j.group_by("segment").agg(pl.col("amount").sum()).sort("segment")
        return " ".join(f"{r[0]}:{cents(r[1])}" for r in g.iter_rows())

    def f_month():
        g = o.group_by(pl.col("day").dt.strftime("%Y-%m").alias("m")).agg(pl.len()).sort("m")
        return f"{g.height} {g['len'][0]} {g['len'][-1]}"

    for name, fn in [("filter", f_filter), ("group", f_group), ("bycust", f_bycust),
                     ("top", f_top), ("join", f_join), ("month", f_month)]:
        t, ans = best(fn)
        yield name, t, ans


lib, op, cp = sys.argv[1:4]
for name, t, ans in (run_pandas if lib == "pandas" else run_polars)(op, cp):
    print(f"{name}\t{t:.4f}\t{ans}", flush=True)
