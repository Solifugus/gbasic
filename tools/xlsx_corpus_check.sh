#!/usr/bin/env bash
# Run the xlsx.check oracle over a whole corpus of workbooks and total the
# result. Not part of any test suite: it needs a corpus nobody should have to
# download, and it MEASURES rather than asserts.
#
# Why it exists. An xlsx stores both the formula and the value Excel last
# computed for it, so every formula cell is checkable in isolation against an
# implementation that is not ours -- no dependency graph required, and no
# fixture we wrote. Pointed at the Enron corpus (15,871 workbooks re-saved
# through Excel in 2014, so the cached values are Excel's own output) this has
# reordered the roadmap twice: it is what showed the dominant defects were not
# missing functions at all but shared formulas and cross-sheet references, which
# a scan counting `NAME(` tokens is structurally incapable of seeing.
#
# ONE PROCESS PER FILE, deliberately. A corpus this size reliably contains files
# that crash or hang a reader, and a single long-running process loses the whole
# run when one of them does. Per-file isolation costs startup and buys a scan
# that always finishes, with the failures named.
#
# Usage:
#   tools/xlsx_corpus_check.sh <corpus-dir> [output.tsv] [parallelism]
#
# Writes one line per workbook to the output file and prints totals at the end.
# Safe to interrupt: the partial file is still valid input for the totals, which
# are recomputed from it by tools/xlsx_corpus_report.sh.
set -u

cd "$(dirname "$0")/.."

dir=${1:-/home/solifugus/corpora/enron/extracted}
out=${2:-/tmp/xlsx_corpus_check.tsv}
jobs=${3:-$(( $(nproc 2>/dev/null || echo 4) - 2 ))}
[ "$jobs" -lt 1 ] && jobs=1

if [ ! -d "$dir" ]; then
    printf 'no such corpus directory: %s\n' "$dir" >&2
    exit 1
fi

if ! make >/dev/null 2>&1; then
    printf 'build failed\n' >&2
    exit 1
fi

printf 'scanning %s with %s parallel processes -> %s\n' "$dir" "$jobs" "$out" >&2

# A per-file timeout so one pathological workbook cannot stall the run. Files
# that exceed it are recorded as TIMEOUT rather than silently dropped -- a
# dropped file would quietly shrink the denominator and flatter the agreement
# rate.
scan_one() {
    f=$1
    line=$(timeout 300 ./gbasic tools/xlsx_corpus_check.bas "$f" 2>/dev/null)
    rc=$?
    if [ "$rc" = "124" ]; then
        printf 'TIMEOUT\t%s\n' "$f"
    elif [ "$rc" != "0" ] || [ -z "$line" ]; then
        printf 'ERR\t%s\n' "$f"
    else
        printf '%s\n' "$line"
    fi
}
export -f scan_one

# -print0 / -0 throughout: corpus filenames contain spaces, and a plain `for`
# loop over them splits mid-name.
find "$dir" -maxdepth 1 -type f -name '*.xlsx' -print0 \
    | xargs -0 -P "$jobs" -I{} bash -c 'scan_one "$@"' _ {} \
    > "$out"

printf 'scan complete\n' >&2
exec "$(dirname "$0")/xlsx_corpus_report.sh" "$out"
