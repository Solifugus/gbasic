#!/usr/bin/env bash
#
# edgar_capture.sh — record the EDGAR fixtures Track A needs.
#
# Governed by docs/edgar_suite_development_plan.md (WP-EDG-1). Claude Code
# writes this script; **Matthew runs it** with his own contact identity and is
# responsible for SEC rate compliance. The captured JSON is then checked into
# examples/fixtures/edgar/ and consumed offline by the test suites — tests never
# hit the network (plan §1 hard rules).
#
# What it captures, per the design (edgar_design.md §2):
#   - company_tickers.json           (ticker -> CIK map; www.sec.gov)
#   - submissions/CIK{10}.json       (filing index; data.sec.gov)
#   - companyfacts/CIK{10}.json      (XBRL facts; data.sec.gov)
# for AAPL, JPM, and one small-cap Matthew picks.
#
# Client discipline (edgar_design.md §2, non-negotiable):
#   - a declared User-Agent WITH CONTACT INFO, supplied via $EDGAR_IDENT
#   - polite spacing between requests (this script uses 1 req/sec, well under
#     the SEC's 10 req/sec ceiling)
#
# Usage:
#   EDGAR_IDENT="Your Name your@email" tools/edgar_capture.sh AAPL JPM <SMALLCAP>
#   tools/edgar_capture.sh --print-only            # dry run: print the plan only
#
# With no tickers given it defaults to "AAPL JPM" and reminds you to append the
# small-cap of your choosing (the plan reserves that pick for you).

set -euo pipefail

# --- repo-root anchoring ----------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTDIR="$REPO_ROOT/examples/fixtures/edgar"
MANIFEST="$OUTDIR/MANIFEST.md"

# --- endpoints (edgar_design.md §2) -----------------------------------------
TICKERS_URL="https://www.sec.gov/files/company_tickers.json"
SUBMISSIONS_BASE="https://data.sec.gov/submissions"
COMPANYFACTS_BASE="https://data.sec.gov/api/xbrl/companyfacts"

RATE_SLEEP=1          # seconds between requests (1 req/sec)

PRINT_ONLY=false
TICKERS=()

usage() {
    sed -n '2,30p' "${BASH_SOURCE[0]}" | sed 's/^#\{0,1\} \{0,1\}//'
}

# --- argument parsing -------------------------------------------------------
# --form4 CIK ACCESSION FILENAME captures a single Form 4 (or any filing
# document) raw XML into examples/fixtures/edgar/form4_sample.xml. Find a recent
# Form 4 in a submissions fixture (form=="4"); the raw XML is the primaryDocument
# with its "xslF345X06/" style prefix stripped. Example:
#   tools/edgar_capture.sh --form4 320193 000114036126025622 form4.xml
FORM4_CIK=""
FORM4_ACCN=""
FORM4_FILE=""
# --doc CIK ACCESSION FILENAME DESTNAME captures ANY archive document into
# examples/fixtures/edgar/DESTNAME (e.g. a 13F information table XML). Same URL
# shape as --form4 but with a caller-chosen destination filename. Example:
#   tools/edgar_capture.sh --doc 1067983 000095012325007658 form13fInfoTable.xml f13_infotable_sample.xml
DOC_CIK=""
DOC_ACCN=""
DOC_FILE=""
DOC_DEST=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --print-only) PRINT_ONLY=true; shift ;;
        --form4)      FORM4_CIK="$2"; FORM4_ACCN="$3"; FORM4_FILE="$4"; shift 4 ;;
        --doc)        DOC_CIK="$2"; DOC_ACCN="$3"; DOC_FILE="$4"; DOC_DEST="$5"; shift 5 ;;
        -h|--help)    usage; exit 0 ;;
        --*)          echo "edgar_capture: unknown option '$1'" >&2; exit 2 ;;
        *)            TICKERS+=("$1"); shift ;;
    esac
done

if [[ ${#TICKERS[@]} -eq 0 ]]; then
    TICKERS=(AAPL JPM)
    echo "edgar_capture: no tickers given; defaulting to AAPL JPM." >&2
    echo "edgar_capture: append your small-cap pick, e.g. 'AAPL JPM <SMALLCAP>'." >&2
fi

# --- identity gate (edgar_design.md §2: declared UA with contact info) -------
# A real fetch MUST have a contact identity. In --print-only we tolerate an
# unset value so the plan is still inspectable, but we flag it loudly.
if [[ -z "${EDGAR_IDENT:-}" ]]; then
    if $PRINT_ONLY; then
        EDGAR_IDENT="<EDGAR_IDENT unset — set before a real run>"
        echo "edgar_capture: WARNING — EDGAR_IDENT is unset; using a placeholder for the dry run." >&2
    else
        echo "edgar_capture: FATAL — EDGAR_IDENT is unset." >&2
        echo "  Set it to your contact identity before capturing, e.g.:" >&2
        echo "    EDGAR_IDENT=\"Jane Roe jane@example.com\" tools/edgar_capture.sh AAPL JPM PLCE" >&2
        echo "  The SEC blocks anonymous automated traffic; a real UA is mandatory." >&2
        exit 1
    fi
fi

# --- Form 4 (single-document) capture mode ----------------------------------
if [[ -n "$FORM4_CIK" ]]; then
    accn_nodash="${FORM4_ACCN//-/}"
    url="https://www.sec.gov/Archives/edgar/data/${FORM4_CIK}/${accn_nodash}/${FORM4_FILE}"
    dest="$OUTDIR/form4_sample.xml"
    if $PRINT_ONLY; then
        printf 'edgar_capture: [form4] fetch %s\n            -> %s\n' "$url" "${dest#$REPO_ROOT/}"
        exit 0
    fi
    mkdir -p "$OUTDIR"
    echo "edgar_capture: [form4] $url" >&2
    curl --fail --silent --show-error --location --compressed \
         --user-agent "$EDGAR_IDENT" "$url" -o "$dest"
    now="$(date -u '+%Y-%m-%d %H:%M:%SZ')"
    printf '| %s | %s | %s |\n' "$url" "${dest#$REPO_ROOT/}" "$now" >> "$MANIFEST"
    echo "edgar_capture: [form4] wrote ${dest#$REPO_ROOT/} and appended MANIFEST." >&2
    exit 0
fi

# --- generic single-document capture mode -----------------------------------
if [[ -n "$DOC_CIK" ]]; then
    accn_nodash="${DOC_ACCN//-/}"
    url="https://www.sec.gov/Archives/edgar/data/${DOC_CIK}/${accn_nodash}/${DOC_FILE}"
    dest="$OUTDIR/${DOC_DEST}"
    if $PRINT_ONLY; then
        printf 'edgar_capture: [doc] fetch %s\n            -> %s\n' "$url" "${dest#$REPO_ROOT/}"
        exit 0
    fi
    mkdir -p "$OUTDIR"
    echo "edgar_capture: [doc] $url" >&2
    curl --fail --silent --show-error --location --compressed \
         --user-agent "$EDGAR_IDENT" "$url" -o "$dest"
    now="$(date -u '+%Y-%m-%d %H:%M:%SZ')"
    printf '| %s | %s | %s |\n' "$url" "${dest#$REPO_ROOT/}" "$now" >> "$MANIFEST"
    echo "edgar_capture: [doc] wrote ${dest#$REPO_ROOT/} and appended MANIFEST." >&2
    exit 0
fi

# --- JSON helper: resolve a ticker to a zero-padded 10-digit CIK ------------
# Reads the already-downloaded company_tickers.json. Prefers jq; falls back to
# python3. Errors loudly if the ticker is absent.
have_jq=false
command -v jq >/dev/null 2>&1 && have_jq=true
have_py=false
command -v python3 >/dev/null 2>&1 && have_py=true

resolve_cik() {   # resolve_cik <ticker> <tickers_json_path>  -> echoes CIK{10}
    local ticker upper file raw
    ticker="$1"; file="$2"
    upper="$(printf '%s' "$ticker" | tr '[:lower:]' '[:upper:]')"
    if $have_jq; then
        raw="$(jq -r --arg t "$upper" \
            'to_entries[] | .value | select((.ticker|ascii_upcase) == $t) | .cik_str' \
            "$file" | head -1)"
    elif $have_py; then
        raw="$(python3 - "$file" "$upper" <<'PY'
import json, sys
data = json.load(open(sys.argv[1]))
t = sys.argv[2]
for row in data.values():
    if str(row.get("ticker","")).upper() == t:
        print(row["cik_str"]); break
PY
)"
    else
        echo "edgar_capture: need jq or python3 to parse company_tickers.json" >&2
        exit 3
    fi
    if [[ -z "$raw" ]]; then
        echo "edgar_capture: ticker '$ticker' not found in company_tickers.json" >&2
        exit 4
    fi
    printf 'CIK%010d' "$raw"
}

# --- MANIFEST bookkeeping ---------------------------------------------------
manifest_init() {
    {
        echo "# EDGAR fixture manifest"
        echo
        echo "Captured by tools/edgar_capture.sh. One row per recorded file."
        echo "Identity used: \`$EDGAR_IDENT\`"
        echo
        echo "| URL | file | captured (UTC) |"
        echo "|---|---|---|"
    } > "$MANIFEST"
}

manifest_add() {  # manifest_add <url> <dest>
    local url dest now
    url="$1"; dest="$2"
    now="$(date -u '+%Y-%m-%d %H:%M:%SZ')"
    printf '| %s | %s | %s |\n' "$url" "${dest#$REPO_ROOT/}" "$now" >> "$MANIFEST"
}

# --- fetch primitive --------------------------------------------------------
fetch() {   # fetch <url> <dest>
    local url dest
    url="$1"; dest="$2"
    if $PRINT_ONLY; then
        printf '  fetch  %s\n         -> %s\n' "$url" "${dest#$REPO_ROOT/}"
        return 0
    fi
    curl --fail --silent --show-error --location --compressed \
         --user-agent "$EDGAR_IDENT" "$url" -o "$dest"
    manifest_add "$url" "$dest"
    sleep "$RATE_SLEEP"
}

# --- plan / run -------------------------------------------------------------
echo "edgar_capture: output dir  $OUTDIR"
echo "edgar_capture: identity    $EDGAR_IDENT"
echo "edgar_capture: tickers     ${TICKERS[*]}"
echo "edgar_capture: rate        1 request / ${RATE_SLEEP}s"
if $PRINT_ONLY; then
    echo "edgar_capture: MODE       --print-only (no network, no files written)"
fi
echo

if ! $PRINT_ONLY; then
    mkdir -p "$OUTDIR"
    manifest_init
fi

# Step 1: the ticker -> CIK map (needed to resolve every other URL).
tickers_dest="$OUTDIR/company_tickers.json"
echo "[1] ticker -> CIK map"
fetch "$TICKERS_URL" "$tickers_dest"

# Step 2: per-filer submissions + companyfacts.
i=1
for ticker in "${TICKERS[@]}"; do
    i=$((i + 1))
    echo "[$i] $ticker"
    if $PRINT_ONLY; then
        cik="CIK{10}"   # resolved from company_tickers.json at real-run time
    else
        cik="$(resolve_cik "$ticker" "$tickers_dest")"
        echo "     resolved $ticker -> $cik"
    fi
    fetch "$SUBMISSIONS_BASE/$cik.json"      "$OUTDIR/submissions_$cik.json"
    fetch "$COMPANYFACTS_BASE/$cik.json"     "$OUTDIR/companyfacts_$cik.json"
done

echo
if $PRINT_ONLY; then
    echo "edgar_capture: dry run complete — nothing fetched or written."
else
    echo "edgar_capture: done. Fixtures in $OUTDIR ; manifest at ${MANIFEST#$REPO_ROOT/}."
fi
