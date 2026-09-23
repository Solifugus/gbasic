#!/usr/bin/env bash
# Re-record the replay fixtures from the live publishers. NOT part of the gate:
# this is the only thing here that touches the network, and it is run
# deliberately. The fixtures are committed so the gate is hermetic.
set -eu
cd "$(dirname "$0")"
curl -sS --max-time 30 "https://markets.newyorkfed.org/api/rates/secured/sofr/last/30.json"   > replay/nyfed_sofr.json
curl -sS --max-time 30 "https://markets.newyorkfed.org/api/rates/unsecured/effr/last/30.json" > replay/nyfed_effr.json
curl -sS -g --max-time 30 "https://api.fiscaldata.treasury.gov/services/api/fiscal_service/v2/accounting/od/avg_interest_rates?page%5Bsize%5D=30&sort=-record_date" > replay/treasury_avg_interest.json
echo "re-recorded; review the diff before committing -- these are dated observations"
