#!/usr/bin/env bash
# Re-record the replay fixtures from the live publishers. NOT part of the gate:
# this is the only thing here that touches the network, and it is run
# deliberately. The fixtures are committed so the gate is hermetic.
set -eu
cd "$(dirname "$0")"
curl -sS --max-time 30 "https://markets.newyorkfed.org/api/rates/secured/sofr/last/30.json"   > replay/nyfed_sofr.json
curl -sS --max-time 30 "https://markets.newyorkfed.org/api/rates/unsecured/effr/last/30.json" > replay/nyfed_effr.json
curl -sS -g --max-time 30 "https://api.fiscaldata.treasury.gov/services/api/fiscal_service/v2/accounting/od/avg_interest_rates?page%5Bsize%5D=30&sort=-record_date" > replay/treasury_avg_interest.json
# The ECB window is FIXED: the expected rates in fx_test.bas were computed from
# this window, over the 2025 year end. replay_lastn/ is the request the library
# does NOT make, kept to show why; re-record it only deliberately.
curl -sS --max-time 60 "https://data-api.ecb.europa.eu/service/data/EXR/D..EUR.SP00.A?startPeriod=2025-12-22&endPeriod=2026-01-09&format=csvdata" > replay/ecb_fx.csv
echo "re-recorded; review the diff before committing -- these are dated observations"
