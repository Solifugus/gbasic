#!/usr/bin/env python3
"""Analytics benchmark data: orders_<N>.csv and customers.csv, deterministic.

orders:    id, customer, region, day, qty, price
           customer 1..10000, region one of 8, day 2025-01-01..2026-12-31,
           qty 1..20, price 1.00..500.00 with two decimals (text, so every
           reader sees the same decimal digits)
customers: id, segment (one of 5), 10000 rows

A 64-bit LCG of our own, not random(), so the bytes never depend on a
Python version.
"""
import sys
import datetime

REGIONS = ["North", "South", "East", "West", "Central", "Coast", "Mountain", "Plains"]
SEGMENTS = ["retail", "wholesale", "online", "government", "education"]


class Lcg:
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFFFFFFFFFF

    def next(self, n):
        self.s = (self.s * 6364136223846793005 + 1442695040888963407) & 0xFFFFFFFFFFFFFFFF
        return (self.s >> 33) % n


def main():
    n = int(sys.argv[1])
    out = sys.argv[2]
    r = Lcg(20261128)
    start = datetime.date(2025, 1, 1)
    with open(f"{out}/orders_{n}.csv", "w", newline="") as f:
        f.write("id,customer,region,day,qty,price\n")
        for i in range(1, n + 1):
            cust = r.next(10000) + 1
            region = REGIONS[r.next(8)]
            day = start + datetime.timedelta(days=r.next(730))
            qty = r.next(20) + 1
            cents = r.next(49901) + 100
            f.write(f"{i},{cust},{region},{day.isoformat()},{qty},{cents // 100}.{cents % 100:02d}\n")
    r = Lcg(7)
    with open(f"{out}/customers.csv", "w", newline="") as f:
        f.write("id,segment\n")
        for i in range(1, 10001):
            f.write(f"{i},{SEGMENTS[r.next(5)]}\n")


main()
