#!/usr/bin/env bash
# Rank what a corpus's UNSUPPORTED cells are blocked on, and what its
# DISAGREEing cells look like. Companion to tools/xlsx_corpus_check.sh, which
# gives the totals; this says where the remaining work is.
#
# The ranking is by the name the EVALUATOR refused (note.blocked_by), not by
# counting function tokens in formula text. SS13.J showed the token count is
# structurally blind -- a formula usually holds several functions and only one
# is the blocker -- and that following it ranked the roadmap wrongly twice.
set -u
cd "$(dirname "$0")/.."
dir=${1:-/home/solifugus/corpora/enron/extracted}
out=${2:-/tmp/xlsx_corpus_blockers.txt}
jobs=${3:-$(( $(nproc 2>/dev/null || echo 4) - 2 ))}
[ "$jobs" -lt 1 ] && jobs=1
make >/dev/null 2>&1 || { echo "build failed" >&2; exit 1; }
one() { timeout 300 ./gbasic tools/xlsx_corpus_blockers.bas "$1" 2>/dev/null; }
export -f one
find "$dir" -maxdepth 1 -type f -name '*.xlsx' -print0 \
  | xargs -0 -P "$jobs" -I{} bash -c 'one "$@"' _ {} > "$out"
printf '\n=== TOP BLOCKERS (unsupported cells, by the name actually refused) ===\n'
grep '^BLOCK ' "$out" | sort | uniq -c | sort -rn | head -40
printf '\n=== DISAGREEING CELLS by leading function (EXPR = operators only) ===\n'
grep '^DISAGREE ' "$out" | sort | uniq -c | sort -rn | head -30
printf '\ntotals: %s blocked, %s disagreeing\n' \
  "$(grep -c '^BLOCK ' "$out")" "$(grep -c '^DISAGREE ' "$out")"
