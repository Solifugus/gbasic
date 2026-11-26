#!/usr/bin/env bash
# `gbasic --doc NAME` and `gbasic --explain CODE`: the binary answering
# questions about itself (2026-11-25, the first of Matthew's "AI aids"), so an
# author asks the build it is using rather than guessing a signature or reading
# another version's docs. Its tables are made by make from docs/reference.md,
# docs/ai/ERRORS.md and src/builtins.c (src/docindex_gen.c); a library's
# function is read from the file a `load` would find.
#
# Tiers:
#   coverage  every builtin this build has, has an entry, except the names in
#             UNDOCUMENTED below -- and every name there must still lack one,
#             so documenting one turns this red until it is taken off the list
#             (the allowlist's negative control).
#   oracle    for every builtin, python3 reads docs/reference.md on its own and
#             finds the line that opens the name's first entry; --doc must
#             print that line. The generator is C, the oracle Python, the two
#             written separately. Skips without python3.
#   entries   by hand: an example carried, a module verb, an entry opening
#             with a bold label, a whole family on one line, a misspelling
#             answered with the nearest names, an unknown name exiting 1.
#   library   `dates.days_between` from the stdlib this binary loads, and from
#             a GBASIC_PATH directory ahead of it, as `load` would; a function
#             the library does not define named with its nearest, and pointed
#             at the builtin of that name.
#   explain   every code src/eval.c raises or warns with answers, each
#             warning with the way to silence or escalate it; a code in two
#             tables gives both; an unknown code exits 1.
# Headless, no network.
. "$(dirname "$0")/portable.sh"
set -u
cd "$(dirname "$0")/.."
make >/dev/null 2>&1 || { echo "FAIL run_self_doc: build failed"; exit 1; }

status=0
pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; status=1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# THE BUILTINS THE REFERENCE DOES NOT DESCRIBE YET, measured 2026-11-25. Each
# is a gap in docs/reference.md, not in this suite: document one and take it
# off the list.
UNDOCUMENTED="ceil copy exp floor from_zone hmac_sha512 lgamma log10 pbkdf2_sha512 percentile principal remove_dir source_outline to_zone warning watchers zone_offset zone_resolve"

# the builtins this build has: the registry, asked name by name
names=$(sed -n '/builtins\[\] = {/,/^    };/p; /dispatch_only\[\] = {/,/^    };/p' src/builtins.c \
        | grep -oE '^ *"[a-z_0-9]+"' | tr -d ' "' | sort -u)
printf 'for each n in split(env("NAMES"), " ")\n    if has_builtin(n) then\n        print n\n    end if\nend for\n' >"$work/has.bas"
built=$(NAMES="$(echo $names)" ./gbasic "$work/has.bas")

echo "--- TIER coverage ---"
missing=""
for n in $built; do
    if ! ./gbasic --doc "$n" >/dev/null 2>&1; then
        missing="$missing $n"
    fi
done
unexpected=""
for n in $missing; do
    case " $UNDOCUMENTED " in *" $n "*) ;; *) unexpected="$unexpected $n" ;; esac
done
stale=""
for n in $UNDOCUMENTED; do
    case " $missing " in *" $n "*) ;; *)
        # a name this build lacks is neither documented nor missing here
        if printf '%s\n' $built | grep -qx "$n"; then stale="$stale $n"; fi ;;
    esac
done
count=$(printf '%s\n' $built | wc -l | tr -d ' ')
if [ -z "$unexpected" ] && [ -z "$stale" ] && [ "$count" -ge 200 ]; then
    pass "$count builtins in this build; every one has an entry but the $(echo $UNDOCUMENTED | wc -w | tr -d ' ') listed"
else
    [ -n "$unexpected" ] && fail "no --doc entry, and not on the UNDOCUMENTED list:$unexpected"
    [ -n "$stale" ] && fail "documented now, so take them off UNDOCUMENTED:$stale"
    [ "$count" -ge 200 ] || fail "only $count builtins found in src/builtins.c"
fi

echo "--- TIER oracle ---"
if command -v python3 >/dev/null 2>&1; then
    python3 - docs/reference.md $built >"$work/oracle.tsv" <<'PY'
import re, sys
lines = open(sys.argv[1], encoding="utf-8").read().split("\n")
fence = False
first = {}
for l in lines:
    t = l.strip()
    if t.startswith("```"):
        fence = not fence
        continue
    if fence:
        continue
    if t.startswith("- **`"):
        head = t[4:]
    elif t.startswith("- **"):
        m = re.match(r"- \*\*[^*]+\*\*:? *(`.*)", t)
        if not m:
            continue
        head = m.group(1)
    elif t.startswith("- `") or t.startswith("**`"):
        head = t[2:]
    elif t.startswith("| `"):
        head = t.split("|")[1]
    else:
        continue
    if not t.startswith("| "):
        head = re.split(r" — | - |: | → |\*\* ", head, maxsplit=1)[0]
    for name in re.findall(r"`([a-z][a-z0-9_.]*)\(", head):
        first.setdefault(name, t)
for name in sys.argv[2:]:
    if name in first:
        print(name + "\t" + first[name])
PY
    checked=0; bad=""
    while IFS=$'\t' read -r name line; do
        checked=$((checked + 1))
        if ! ./gbasic --doc "$name" | grep -qxF -- "$line"; then
            bad="$bad $name"
        fi
    done <"$work/oracle.tsv"
    if [ -z "$bad" ] && [ "$checked" -ge 200 ]; then
        pass "$checked builtins: --doc prints the line python3 found opening each one's first entry"
    else
        fail "oracle ($checked checked): --doc does not print the entry line for:$bad"
    fi
else
    printf 'SKIP oracle (no python3)\n'
fi

echo "--- TIER entries ---"
want() {   # want <label> <name> <substring>
    if ./gbasic --doc "$2" 2>&1 | grep -qF -- "$3"; then pass "$1"; else fail "$1: --doc $2 lacks: $3"; fi
}
want "an entry carries its example"    starts_with 'starts_with("hello", "he")     '"'"' true'
want "and names its section"           starts_with '(docs/reference.md: String Helpers)'
want "a module verb"                   process.start '**`process.start(options)`** → a **process handle**'
want "an entry opening with a label"   csv.line '- **Writing**: `csv.line(values, options)`'
want "a family on one line"            sha384 '`sha256(s)`, `sha384(s)`, `sha512(s)`, `sha1(s)`'
want "a misspelling names the nearest" starts_wth 'did you mean: starts_with'
if ./gbasic --doc no_such_builtin >/dev/null 2>&1; then
    fail "an unknown name should exit 1"
else
    pass "an unknown name exits 1"
fi

echo "--- TIER library ---"
tree="$(pwd -P)"
out=$(unset GBASIC_PATH; ./gbasic --doc dates.days_between)
case "$out" in
    *"dates.days_between(a, b)"*"(library dates, $tree/stdlib/dates.bas:"*"b - a in days, signed"*)
        pass "a library function, its signature and comment, from the stdlib this binary loads" ;;
    *) fail "dates.days_between from the binary's stdlib: $out" ;;
esac
mkdir -p "$work/lib"
printf "library dates\n    ' the planted copy\n    function days_between(a, b)\n        return 0\n    end function\nend library\n" >"$work/lib/dates.bas"
out=$(GBASIC_PATH="$work/lib" ./gbasic --doc dates.days_between)
case "$out" in
    *"$work/lib/dates.bas:3"*"the planted copy"*) pass "GBASIC_PATH ahead of it, as load has it" ;;
    *) fail "GBASIC_PATH should win: $out" ;;
esac
out=$(./gbasic --doc stats.zscor)
case "$out" in
    *"defines no function zscor"*"did you mean: stats.zscore"*) pass "a function the library lacks, with its nearest" ;;
    *) fail "stats.zscor: $out" ;;
esac
want "a builtin asked for under a library" stats.mean 'mean is a builtin, called without a qualifier'

echo "--- TIER explain ---"
codes=$( { grep -oE '(runtime_error_raise|raise_fmt|warn_fmt|warn_fmt_at)\([^;]*' src/eval.c | grep -oE '[, (]([12][0-9]{3}|5[0-9]{3})[,)]' | grep -oE '[0-9]{4}'; } | sort -u)
nocode=""; nowarn=""
for c in $codes; do
    out=$(./gbasic --explain "$c" 2>&1) || { nocode="$nocode $c"; continue; }
    case "$c" in 21[0-9][0-9]) case "$out" in *"on warning stop"*) ;; *) nowarn="$nowarn $c" ;; esac ;; esac
done
if [ -z "$nocode" ] && [ -z "$nowarn" ] && [ "$(echo $codes | wc -w)" -ge 20 ]; then
    pass "$(echo $codes | wc -w | tr -d ' ') codes src/eval.c raises or warns with each answer; each warning says how to escalate it"
else
    [ -n "$nocode" ] && fail "--explain has no entry for:$nocode"
    [ -n "$nowarn" ] && fail "warnings without the on warning line:$nowarn"
fi
out=$(./gbasic --explain 1001)
case "$out" in
    *"1001 as a diagnostic code"*"1001 as a runtime error.code:"*"Undefined variable"*) pass "a code in two tables gives both" ;;
    *) fail "1001: $out" ;;
esac
case "$(./gbasic --explain 1003)" in
    *"error.source: \`invalid function call\`"*) pass "1003 lists its error.source domains" ;;
    *) fail "1003 should list its domains" ;;
esac
if ./gbasic --explain 9999 >/dev/null 2>&1; then fail "an unknown code should exit 1"; else pass "an unknown code exits 1"; fi

[ "$status" = 0 ] && echo "PASS tests/run_self_doc.sh" || echo "FAIL tests/run_self_doc.sh"
exit $status
