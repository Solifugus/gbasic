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

printf '\n=== MISMATCH SHAPE (computed -> cached) ===\n'
grep '^SHAPE ' "$out" | sort | uniq -c | sort -rn | head -20
printf '\n=== WHEN WE PRODUCE AN ERROR AND EXCEL DID NOT ===\n'
grep '^ERRC ' "$out" | sort | uniq -c | sort -rn | head -20
printf '\n=== leading function, context only ===\n'
grep '^FUNC ' "$out" | sort | uniq -c | sort -rn | head -20
printf '\ntotal disagreeing cells: %s\n' "$(grep -c '^SHAPE ' "$out")"
