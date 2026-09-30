#!/usr/bin/env bash
# AN UNKNOWN STRING ESCAPE KEEPS BOTH CHARACTERS AND WARNS (2026-09-30).
#
# It used to FAIL THE PARSE, and the cost was concrete: every regex in
# docs/text_design.md was untypable. `match(s, "\$([0-9,]+)\.([0-9]{2})")` died
# on the `\$` before the regex engine saw it -- while the SAME document
# correctly states that the regex dialect accepts `\d`, `\w` and `\s`. Both
# statements were true and about different layers (the REGEX dialect takes them,
# the STRING LITERAL carrying it would not), and nothing said so.
#
# Reported by the gbasic-books session while planning Volume 2. They offered
# three fixes; Matthew took the middle one. Passing unknown escapes through
# SILENTLY was rejected -- an unknown escape is also a good typo detector -- so
# they are kept AND reported, and the author decides.
#
# TWO MECHANISMS, BECAUSE THERE ARE TWO STAGES, and the asymmetry is asserted
# here so it cannot drift into an accident:
#
#   - an ORDINARY literal is unescaped while the file is PARSED, before any
#     statement has run, so there are no frames for `on warning` to consult. It
#     goes to the diagnostics sink as a WARNING-severity entry -- the first
#     non-error severity anything in this tree has ever emitted, though the sink
#     has been able to carry one since it was written.
#   - a MODIFIER literal is unescaped while the program RUNS, so the real
#     warning channel exists and `on warning stop` governs it (code 2109).
#
# Both print. Only the second can be escalated, and THAT IS THE KNOWN GAP: a
# project cannot make an unknown escape in an ordinary literal fatal. Asserted
# below rather than left as prose, so that closing it is a deliberate act.
. "$(dirname "$0")/portable.sh"
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

status=0
checks=0
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

run() {   # run <source>; sets $rc $out $err
    printf '%s\n' "$1" >"$tmp/p.bas"
    rc=0
    ./gbasic "$tmp/p.bas" >"$tmp/o" 2>"$tmp/e" || rc=$?
    out="$(cat "$tmp/o")"
    err="$(cat "$tmp/e")"
}

ok()   { checks=$((checks + 1)); printf 'ok   %s\n' "$1"; }
bad()  { checks=$((checks + 1)); printf 'MISMATCH %s\n  %s\n' "$1" "$2"; status=1; }

echo "--- TIER 1: an unknown escape is kept, warned about, and the program runs ---"
run 'print "a\db"
print len("a\db")'
[ "$rc" = "0" ] && [ "$out" = "a\\db
4" ] && printf '%s' "$err" | grep -q 'unknown escape \\d' \
    && ok 'unknown escape kept as two characters, warned, exit 0' \
    || bad 'unknown escape' "rc=$rc out=[$out] err=[$err]"

echo "--- TIER 2: THE PAYOFF -- the documented regex runs verbatim ---"
# Copied from docs/text_design.md §3. This is the case the old behaviour made
# impossible, and it asserts the ANSWER, not merely that it parsed.
run 'm = match("balance: $1,500.00 due", "\$([0-9,]+)\.([0-9]{2})")
print m.text
print m.groups[0] + " / " + m.groups[1]'
[ "$rc" = "0" ] && [ "$out" = "\$1,500.00
1,500 / 00" ] \
    && ok 'the documented regex example works as written' \
    || bad 'documented regex' "rc=$rc out=[$out]"

echo "--- TIER 3: CONTROLS -- known escapes are untouched ---"
# Without these, "unknown escapes are kept" is satisfied by a build that stopped
# interpreting escapes at all.
run 'print len("a\nb")
print len("a\tb")
print len("a\"b")
print len("a\\b")
print len("a\u{41}b")'
[ "$rc" = "0" ] && [ "$out" = "3
3
3
3
3" ] && [ -z "$err" ] \
    && ok 'n, t, quote, backslash and \u{} all still collapse to one character, silently' \
    || bad 'known escapes' "rc=$rc out=[$out] err=[$err]"

echo "--- TIER 4: CONTROLS -- malformed unicode is STILL REFUSED ---"
# The relaxation is narrow: an UNKNOWN escape is now advice, a MALFORMED \u{}
# is still an error. Without this tier, "escapes were relaxed" could quietly
# mean all of them.
for pair in 'print "\u{0}"::not allowed in a literal' \
            'print "\u{D800}"::surrogate' \
            'print "\u41"::must be followed by {' \
            'print "\u{}"::needs hex digits'; do
    src="${pair%%::*}"; want="${pair##*::}"
    run "$src"
    if [ "$rc" != "0" ] && printf '%s' "$err" | grep -q "$want"; then
        ok "still refused: $src"
    else
        bad "should still be refused: $src" "rc=$rc err=[$err]"
    fi
done

echo "--- TIER 5: it is a WARNING, not an error, in both renderings ---"
# The first warning-severity diagnostic this tree has ever emitted. It printed
# as "runtime error" until gb_diag_format was taught to prefer severity over the
# code's stage word -- which is worse than saying nothing at all.
run 'print "a\db"'
printf '%s' "$err" | grep -q '^warning at ' \
    && ok 'the CLI prints it as a warning, not as an error' \
    || bad 'CLI rendering' "err=[$err]"
printf '%s\n' 'print "a\db"' >"$tmp/p.bas"
js="$({ ./gbasic --json-diagnostics "$tmp/p.bas" 2>&1 >/dev/null || true; } | head -1)"
printf '%s' "$js" | python3 -c 'import sys,json; d=json.loads(sys.stdin.read()); sys.exit(0 if d["severity"]=="warning" else 1)' 2>/dev/null \
    && ok '--json-diagnostics carries severity "warning"' \
    || bad 'json severity' "$js"

echo "--- TIER 6: the two stages, and the gap between them ---"
# The modifier form runs, so `on warning stop` reaches it.
run 'on warning stop
p {split "\d"}= "a1b"
print "continued"'
[ "$rc" != "0" ] && [ -z "$out" ] \
    && ok 'a modifier literal CAN be escalated by `on warning stop` (2109)' \
    || bad 'modifier escalation' "rc=$rc out=[$out]"
# ...and the ordinary form does not, because it is decided before anything runs.
# ASSERTED AS THE KNOWN GAP: if this ever starts failing, the gap has been
# closed and this tier should become the opposite assertion.
run 'on warning stop
print "a\db"'
[ "$rc" = "0" ] \
    && ok 'KNOWN GAP: `on warning stop` does NOT reach an ordinary literal (parse-time)' \
    || bad 'known gap changed' "rc=$rc -- if escalation now works, invert this tier"

printf '\nchecks: %d\n' "$checks"
if [ "$status" = 0 ]; then printf 'mismatches: 0\n'; else printf 'FAILED\n'; fi
exit "$status"
