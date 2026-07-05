#!/usr/bin/env bash
#
# screener_sample_build.sh — build the truncated companyfacts sample the screener
# ingest test consumes (WP-SCR-1).
#
# The real nightly bulk file is https://www.sec.gov/Archives/edgar/daily-index/
# xbrl/companyfacts.zip — gigabytes, one CIK{10}.json per filer. gBASIC has no
# DEFLATE, so `screener.ingest` consumes an ALREADY-EXTRACTED directory of those
# JSON files (the user runs `unzip companyfacts.zip -d dir`; edgar_design.md §8.5
# honest fallback — keeps the core untouched). This script produces the equivalent
# extracted directory for the three captured filers, TRUNCATED to a few concepts
# and the last handful of data points each so the fixture stays tiny and the
# `latest_period` scan has something real to find.
#
# Input : examples/fixtures/edgar/companyfacts_CIK*.json  (full captures)
# Output: examples/fixtures/edgar/companyfacts_sample/CIK{10}.json  (truncated)
#
# Deterministic and offline — safe to re-run; overwrites the sample dir.

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
FIXDIR="$REPO_ROOT/examples/fixtures/edgar"
OUTDIR="$FIXDIR/companyfacts_sample"

command -v python3 >/dev/null 2>&1 || { echo "screener_sample_build: needs python3" >&2; exit 1; }

mkdir -p "$OUTDIR"
rm -f "$OUTDIR"/CIK*.json

python3 - "$FIXDIR" "$OUTDIR" <<'PY'
import json, sys, glob, os

fixdir, outdir = sys.argv[1], sys.argv[2]
# concepts kept if present (common across industrials + a bank); last N entries each.
KEEP = ["Assets", "Liabilities", "StockholdersEquity", "NetIncomeLoss",
        "Revenues", "RevenueFromContractWithCustomerExcludingAssessedTax"]
LAST_N = 4

for path in sorted(glob.glob(os.path.join(fixdir, "companyfacts_CIK*.json"))):
    d = json.load(open(path))
    cik10 = "%010d" % int(d["cik"])
    ug = d.get("facts", {}).get("us-gaap", {})
    kept = {}
    for concept in KEEP:
        if concept in ug:
            units = ug[concept].get("units", {})
            newunits = {}
            for uk, entries in units.items():
                newunits[uk] = entries[-LAST_N:]
            kept[concept] = {"label": ug[concept].get("label", concept), "units": newunits}
    out = {"cik": d["cik"], "entityName": d["entityName"], "facts": {"us-gaap": kept}}
    dest = os.path.join(outdir, "CIK%s.json" % cik10)
    with open(dest, "w") as fh:
        json.dump(out, fh, separators=(",", ":"))
    print("wrote %s (%d concepts)" % (os.path.basename(dest), len(kept)))
PY

echo "screener_sample_build: sample dir at ${OUTDIR#$REPO_ROOT/}"
