#!/usr/bin/env bash
# THE CAPABILITY SURFACE, AND THE TRIPWIRE THAT KEEPS IT COMPLETE
# (docs/library_trust_design.md §1.3).
#
# `gbasic --capabilities` reports what this interpreter can touch, so a scanner
# in another project DERIVES the surface instead of hardcoding it. The whole
# scheme rests on a capability scan being a PROOF rather than a guess, and the
# way that proof fails is NOT a wrong label -- it is a MISSING one. The day
# gBASIC gains a twelfth network module and the table does not know, a library
# using it scans as harmless and every scan after that is silently incomplete.
#
# So this suite's load-bearing tier is COVERAGE, in BOTH directions, against the
# two lists the interpreter already maintains for its own reasons:
#
#   library_is_native_qualifier (src/eval.c)  -- maintained because alias
#       collision refusal depends on it, so it cannot quietly rot
#   dispatch_only[] (src/builtins.c)          -- maintained because has_builtin
#       depends on it, which is what made THREE missing names visible on
#       2026-09-30
#
# Deriving from lists that something else already needs is the same argument
# eval_module_needs_load's own comment makes, and the same one run_docs_gate's
# has_builtin tier makes: a list kept only for this purpose is a list that rots.
. "$(dirname "$0")/portable.sh"
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

status=0
checks=0
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

ok()  { checks=$((checks + 1)); printf 'ok   %s\n' "$1"; }
bad() { checks=$((checks + 1)); printf 'MISMATCH %s\n  %s\n' "$1" "$2"; status=1; }

./gbasic --capabilities >"$tmp/reported" 2>/dev/null || true
cut -f1 "$tmp/reported" | sort >"$tmp/names"

# --- the two source lists, read out of the C ---------------------------------
sed -n '/static int library_is_native_qualifier/,/^}/p' src/eval.c \
  | grep -oE '"[a-z_]+"' | tr -d '"' | sort -u >"$tmp/qualifiers"
sed -n '/static const char \*dispatch_only\[\]/,/};/p' src/builtins.c \
  | grep -oE '"[a-z_]+"' | tr -d '"' | sort -u >"$tmp/verbs"

echo "--- TIER 1: the report is not empty, and the scrapers matched ---"
# A scraper that matches nothing reports a clean run, which is the failure mode
# every derive-from-source check in this tree has had to defend against.
for pair in "reported:30" "qualifiers:15" "verbs:9"; do
    f="${pair%%:*}"; floor="${pair##*:}"
    n="$(grep -c . "$tmp/$f" || true)"
    [ "${n:-0}" -ge "$floor" ] \
        && ok "$f has $n entries (floor $floor)" \
        || bad "$f has only $n entries" "the scraper stopped matching, it did not pass"
done

echo "--- TIER 2: COVERAGE, both directions (the load-bearing tier) ---"
missing="$(comm -23 "$tmp/qualifiers" "$tmp/names" || true)"
[ -z "$missing" ] \
    && ok "every native qualifier has a capability classification" \
    || bad "native qualifiers with NO classification" "$(printf '%s' "$missing" | tr '\n' ' ')-- add them to gb_capability_table in src/eval.c; until then a library using one scans as harmless"
extra="$(comm -13 "$tmp/qualifiers" "$tmp/names" | grep -vxF -f "$tmp/verbs" | grep -vxE 'make_dir|atomic_replace|send|receive|self|spawn' || true)"
[ -z "$extra" ] \
    && ok "the table names nothing that is not a real qualifier, verb or actor primitive" \
    || bad "table names something unknown" "$(printf '%s' "$extra" | tr '\n' ' ')"

vmissing="$(comm -23 "$tmp/verbs" "$tmp/names" || true)"
[ -z "$vmissing" ] \
    && ok "every file/dir verb has a capability classification" \
    || bad "file verbs with NO classification" "$(printf '%s' "$vmissing" | tr '\n' ' ')"

echo "--- TIER 3: every label is from the declared vocabulary ---"
# Catches a typo (`nett`), which would make a scanner's filter silently miss it.
bad_label="$(cut -f2 "$tmp/reported" | tr ' ' '\n' | sort -u \
  | grep -vxE 'process|net|net:listen|db|fs:read|fs:write|display|actors|ffi|-' || true)"
[ -z "$bad_label" ] \
    && ok "no label outside the vocabulary" \
    || bad "label outside the vocabulary" "$(printf '%s' "$bad_label" | tr '\n' ' ')"

echo "--- TIER 4: CONTROLS -- specific classifications that must be right ---"
# Without these, coverage is satisfied by labelling everything "-".
want() {
    checks=$((checks + 1))
    got="$(awk -F'\t' -v n="$1" '$1==n {print $2}' "$tmp/reported")"
    [ "$got" = "$2" ] && printf 'ok   %s -> %s\n' "$1" "$got" \
        || { printf 'MISMATCH %s: got [%s], want [%s]\n' "$1" "$got" "$2"; status=1; }
}
want process   "process"
want webclient "net"
want webserver "net:listen"
want sqlite    "db fs:write"
want write     "fs:write"
want read      "fs:read"
want spawn     "actors"
# THE ONE INSPECTION GOT WRONG. `xml` looks like a string parser; it carries
# xml.parse_file, xml.read and xml.reader, so it reads the filesystem. Pinned
# because the next person will make the same assumption.
want xml       "fs:read"
# `gi` can instantiate anything with a typelib, so its reach is NOT bounded by
# this table and the label has to say so.
want gi        "ffi display"
# And the control that stops "label everything dangerous": a module with no
# external effect reports none.
want money     "-"
want timer     "-"

printf '\nchecks: %d\n' "$checks"
if [ "$status" = 0 ]; then printf 'mismatches: 0\n'; else printf 'FAILED\n'; fi
exit "$status"
