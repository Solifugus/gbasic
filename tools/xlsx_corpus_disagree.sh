#!/usr/bin/env bash
# Rank a corpus's remaining DISAGREEMENTS by the SHAPE of the mismatch.
#
# Companion to xlsx_corpus_blockers.sh, which ranks what is REFUSED. Once the
# refusals stop being the dominant pool, this is the instrument that says where
# the wrong ANSWERS are -- and it deliberately ranks by mismatch shape rather
# than by function name, because a cell disagrees because of what the whole
# formula did and its leading function is rarely the cause.
set -u
cd "$(dirname "$0")/.."
dir=${1:-/home/solifugus/corpora/enron/extracted}
out=${2:-/tmp/xlsx_corpus_disagree.txt}
jobs=${3:-$(( $(nproc 2>/dev/null || echo 4) - 2 ))}
[ "$jobs" -lt 1 ] && jobs=1
make >/dev/null 2>&1 || { echo "build failed" >&2; exit 1; }

# Frozen binary + per-worker files, for the reasons the sibling scripts record:
# a concurrent `make clean` once faked 7,797 read errors, and shared-stdout
# interleaving spliced lines and split one bucket's count across two rows.
frozen=$(mktemp -d)/gbasic
cp ./gbasic "$frozen"
part=$(mktemp -d)
trap 'rm -rf "$part" "$(dirname "$frozen")"' EXIT
export part frozen
one() { timeout 300 "$frozen" tools/xlsx_corpus_disagree.bas "$1" 2>/dev/null >"$part/$$.$RANDOM.txt"; }
export -f one
find "$dir" -maxdepth 1 -type f -name '*.xlsx' -print0 \
  | xargs -0 -P "$jobs" -I{} bash -c 'one "$@"' _ {}
cat "$part"/*.txt > "$out" 2>/dev/null

# Every bucket is reported as CELLS and as DISTINCT WORKBOOKS. Cells alone
# mislead badly on this corpus -- one formula template filled down tens of
# thousands of rows outvotes a thousand separate workbooks -- and acting on the
# cell count alone is how the 1900-serial fix came to be estimated at ~198,000
# cells when it was worth 25,320 (§13.X). A cause present in many workbooks is
# usually the more general defect, even when its cell count is smaller.
rank() { # label key-prefix
    printf '\n=== %s ===\n' "$1"
    printf '%10s %8s  %s\n' CELLS BOOKS BUCKET
    grep "^$2 " "$out" | awk -F'\t' '
        { c[$1]++; seen[$1 SUBSEP $2] = 1 }
        END {
            for (k in seen) { split(k, p, SUBSEP); b[p[1]]++ }
            for (k in c) printf "%10d %8d  %s\n", c[k], b[k], k
        }' | sort -rn | head -20
}
rank "MISMATCH SHAPE (computed -> cached)" SHAPE
rank "WHEN WE PRODUCE AN ERROR AND EXCEL DID NOT" ERRC
rank "leading function, context only" FUNC
printf '\ntotal disagreeing cells: %s in %s workbooks\n' \
  "$(grep -c '^SHAPE ' "$out")" \
  "$(grep '^SHAPE ' "$out" | cut -f2 | sort -u | wc -l)"
