#!/usr/bin/env bash
# DOGFOOD.md's accepted-limitations ledger, held to the truth by RUNNING it.
#
# WHY THIS EXISTS. On 2026-09-01 five of the fourteen bullets under "Open --
# accepted as documented limitations (no action planned)" were FALSE, and one
# of them -- "a raise cannot be caught" -- carried a whole doctrine with it
# (pre-validate everything), a doctrine PLAT-ERR had retired. The others named
# line continuation, keywords after a dot, `call(args) = value`, and
# `find`/`match` with the sentinels. Every one had been fixed by a shipped
# phase with its own passing suite sitting in the same tree.
#
# None of that was catchable by reading, and reading is what had been tried:
# the entries read as authoritative, they are cited as design justification in
# other files (`stdlib/consolidate.bas` chose a field name over one of them),
# and nothing anywhere connected a bullet to the code that would falsify it.
# This is the exact rot PLAT-DEBT built run_docs_gate.sh for -- UNLEARN.md
# called `arr[i]` and `append` O(n^2) for six weeks after they became linear --
# but DOGFOOD.md lives at the repo root and that gate reads docs/.
#
# SO EVERY LIVE BULLET IS A NEGATIVE CONTROL. Each probe asserts the
# limitation STILL HOLDS, and goes RED WHEN IT IS FIXED, naming the bullet to
# strike. That is the same shape run_docs_gate uses for a performance claim:
# a claim that something IS slow rides a control that fails once it is not.
# A ledger of limitations nobody has run is worse than no ledger, because it
# is trusted.
#
# TIERS
#   PROBE   -- one executable check per live bullet. Red means FIXED, not
#              broken; the message says so and names the file to edit.
#   COVER   -- every live bullet is claimed by exactly one probe or is named
#              in NOT_MECHANICAL below with a reason, and every probe finds
#              its bullet. Adding a bullet without a probe fails here, which
#              is what stops the ledger growing back into prose nobody checks.
#   CONTROL -- THE TIER THAT MAKES THE OTHERS MEAN ANYTHING. Every bullet
#              already STRUCK THROUGH gets the opposite check: the limitation
#              must be provably GONE. Without it a probe could pass for the
#              wrong reason -- several assert "the output does NOT say X",
#              which is also satisfied by a binary that failed to run at all --
#              and the whole suite would be a row of green lines asserting
#              nothing, which is the exact failure the ledger already had.
#              It doubles as a regression guard on the five phases that did
#              the fixing (PLAT-ERR, PLAT-BRACE, keyword fields, PLAT-EQ,
#              PLAT-CONT), each of which owns a suite of its own but none of
#              which knows this ledger cites it.
#
# Headless, GI-independent. Never skips except the two gi bullets, which need
# a typelib.

set -u
cd "$(dirname "$0")/.."

LEDGER=DOGFOOD.md
SECTION_START='^### Open — accepted as documented limitations'
SECTION_END='^## Seed entries'

status=0
pass=0
skip=0

fail() { printf 'FAIL %s\n' "$*"; status=1; }
ok()   { printf 'PASS %s\n' "$*"; pass=$((pass + 1)); }
skipped() { printf 'SKIP %s\n' "$*"; skip=$((skip + 1)); }

# Red means the limitation is GONE. Say that, loudly, so nobody "fixes" the test.
fixed() {
    printf 'FAIL %s\n' "$1"
    printf '     THE LIMITATION IS GONE. This suite is a negative control: it\n'
    printf '     fails when the thing it guards gets fixed. Strike the bullet in\n'
    printf '     %s (accepted-limitations section), note which phase\n' "$LEDGER"
    printf '     resolved it, and remove the probe here.\n'
    status=1
}

if [ ! -x ./gbasic ]; then
    make >/dev/null 2>&1 || { fail "run_limitations: build failed"; exit 1; }
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

run() { GBASIC_PATH=stdlib ./gbasic "$1" 2>&1; }

# --- PROBE -------------------------------------------------------------------
#
# Each entry: KEY|marker substring that identifies the bullet in the ledger.
# The probe function is probe_<KEY>.

PROBES="
postfix_modifier|do not work postfix in expression position
exponent_literal|No exponent literal
dir_ref_and_make_dir|\`exists\` rejects a dir reference
atomic_replace_inode|gives dest the temp's inode
callresult_method_stmt|method call on a call-result receiver
unnormalized_load_path|print the load path unnormalized
gi_emit|No \`gi.emit\`
"

# Bullets that no program can decide, each with the reason. This list is the
# escape hatch and it is deliberately tiny -- an exemption has to be EARNED by
# being genuinely undecidable here, not merely inconvenient to write.
# KEY|marker|reason. The key column is not decoration: without it the valgrind
# row would begin a line with the bare word `valgrind`, and
# run_valgrind_policy.sh's tripwire -- correctly -- reads that as a suite
# typing its own valgrind flags. The tripwire is right to be strict; this file
# is the one that has to accommodate it.
NOT_MECHANICAL="
gi_static|gi cannot call STATIC class functions|needs a live typelib and a display; tests/run_gi.sh owns this surface
vg_riscv|valgrind does not exist for riscv64|a fact about another architecture, unobservable from this one
"

probe_postfix_modifier() {
    printf 'd = "2026-01-01"{date}\nprint d.year\n' > "$WORK/p.bas"
    if run "$WORK/p.bas" | grep -q 'parse error'; then
        ok "postfix .date in expression position is still a parse error"
    else
        fixed "postfix .date in expression position now parses"
    fi
}

probe_exponent_literal() {
    printf 'print 1e20\n' > "$WORK/p.bas"
    if run "$WORK/p.bas" | grep -q 'unknown duration unit'; then
        ok "1e20 still lexes as a duration (no exponent literal)"
    else
        fixed "1e20 no longer lexes as a duration"
    fi
}

probe_dir_ref_and_make_dir() {
    # Two halves of one bullet; both must still hold.
    printf 'd {dir}= "%s"\nprint exists(d)\n' "$WORK" > "$WORK/p.bas"
    local a=1 b=1
    run "$WORK/p.bas" | grep -q 'exists expects a file reference' && a=0

    printf 'print make_dir("%s/fresh")\nprint make_dir("%s/fresh")\n' "$WORK" "$WORK" > "$WORK/p.bas"
    run "$WORK/p.bas" | grep -q 'could not create directory' && b=0

    if [ $a = 0 ] && [ $b = 0 ]; then
        ok "exists still rejects a dir reference, make_dir still not idempotent"
    elif [ $a != 0 ] && [ $b != 0 ]; then
        fixed "exists accepts a dir reference AND make_dir is idempotent"
    elif [ $a != 0 ]; then
        fixed "exists now accepts a dir reference (make_dir half still stands)"
    else
        fixed "make_dir is now idempotent (exists half still stands)"
    fi
}

probe_atomic_replace_inode() {
    mkdir -p "$WORK/ar"
    printf 'old\n' > "$WORK/ar/dest"; chmod 640 "$WORK/ar/dest"
    printf 'new\n' > "$WORK/ar/tmp";  chmod 600 "$WORK/ar/tmp"
    local before_inode before_perm temp_inode after_inode after_perm
    before_inode=$(stat -c %i "$WORK/ar/dest")
    before_perm=$(stat -c %a "$WORK/ar/dest")
    temp_inode=$(stat -c %i "$WORK/ar/tmp")
    printf 'print atomic_replace("%s/ar/tmp", "%s/ar/dest")\n' "$WORK" "$WORK" > "$WORK/p.bas"
    run "$WORK/p.bas" >/dev/null
    after_inode=$(stat -c %i "$WORK/ar/dest")
    after_perm=$(stat -c %a "$WORK/ar/dest")

    # The bullet also claims there is no chmod/lstat to compose the safe form.
    printf 'print has_builtin("chmod")\nprint has_builtin("lstat")\n' > "$WORK/q.bas"
    local composable
    composable=$(run "$WORK/q.bas" | grep -c true)

    if [ "$after_inode" = "$temp_inode" ] && [ "$after_perm" != "$before_perm" ] \
       && [ "$composable" = 0 ]; then
        ok "atomic_replace still hands dest the temp's inode ($before_inode -> $after_inode, perms $before_perm -> $after_perm) and there is still no chmod/lstat"
    elif [ "$composable" != 0 ]; then
        fixed "chmod/lstat now exist, so the safe form is composable"
    else
        fixed "atomic_replace now preserves dest's inode or permissions"
    fi
}

probe_callresult_method_stmt() {
    printf 'function f()\n  return [1,2]\nend function\nf().count\n' > "$WORK/p.bas"
    if run "$WORK/p.bas" | grep -q 'parse error'; then
        ok "a method call on a call-result receiver is still not a statement"
    else
        fixed "a method call on a call-result receiver now parses as a statement"
    fi
}

probe_unnormalized_load_path() {
    mkdir -p "$WORK/sub"
    printf 'library probe\n    function boom()\n        error "deliberate"\n    end function\nend library\n' > "$WORK/lib.bas"
    printf 'load probe from "../lib.bas"\nprint probe.boom()\n' > "$WORK/sub/use.bas"
    if run "$WORK/sub/use.bas" | grep -q 'sub/\.\./lib\.bas'; then
        ok "library diagnostics still print the load path unnormalized"
    else
        fixed "library diagnostics now normalize the load path"
    fi
}

probe_gi_emit() {
    printf 'load gi\nprint 1\n' > "$WORK/p.bas"
    if run "$WORK/p.bas" | grep -q 'not available in this build'; then
        skipped "gi.emit (gi not in this build)"
        return
    fi
    # gi.* is dispatched by name in eval.c; `emit` is not among the verbs.
    printf 'load gi\non error goto next\nx = gi.emit(1, "a")\nif error then\n  print "refused: " + error.message\n  error.clear()\nelse\n  print "gi.emit ran"\nend if\n' > "$WORK/p.bas"
    if run "$WORK/p.bas" | grep -q 'gi.emit ran'; then
        fixed "gi.emit now exists"
    else
        ok "gi still has no emit verb"
    fi
}

while IFS='|' read -r key marker; do
    [ -z "$key" ] && continue
    "probe_$key"
done <<< "$(printf '%s\n' "$PROBES" | sed '/^$/d')"


# --- CONTROL: the struck bullets, which must now be FALSE ---------------------
#
# Each entry: KEY|marker substring identifying the STRUCK bullet it belongs to.

CONTROLS="
raise_catchable|raise cannot be caught
call_result_compare|misparses as a modifier clause
keyword_after_dot|not after a dot
sentinel_find|misses with \`nothing\`
line_continuation|No line continuation
"

# Red here means a fix REGRESSED, or this suite stopped actually running
# programs. Either way the probes above cannot be trusted this run.
regressed() {
    printf 'FAIL %s\n' "$1"
    printf '     A LIMITATION THAT WAS FIXED IS BACK, or this suite is no longer\n'
    printf '     running gBASIC at all. Until this passes, every PASS above is\n'
    printf '     unverified -- the probes assert an ABSENCE and an absence is\n'
    printf '     also what a broken harness produces.\n'
    status=1
}

control_raise_catchable() {
    # PLAT-ERR: a function catches a raise and returns a fallback. This is the
    # case the pre-PLAT-ERR model could not pass at all.
    cat > "$WORK/c.bas" <<'EOF'
function safe_div(a, b)
  on error goto next
  r = a / b
  if error then
    error.clear()
    return "fallback"
  end if
  return r
end function
print safe_div(10, 2)
print safe_div(10, 0)
print "caller continued"
EOF
    out=$(run "$WORK/c.bas")

    # The bullet named `on error resume next` by name, so pin that the spelling
    # is GONE as well as that catching works. Note what did and did not change:
    # failure-as-a-value did NOT go away -- try_decode and the probe builtins
    # still take that route and it is still right for them -- it simply stopped
    # being the ONLY route, because a raise became catchable. `resume next` is
    # the part that was deleted outright, and a grammar still accepting it would
    # mean the replacement had not actually happened.
    printf 'on error resume next\nprint 1\n' > "$WORK/c2.bas"
    old_spelling=$(run "$WORK/c2.bas")

    if printf '%s' "$out" | grep -q 'fallback' \
       && printf '%s' "$out" | grep -q 'caller continued' \
       && printf '%s' "$old_spelling" | grep -q 'expecting GOTO or STOP'; then
        ok "CONTROL: a raise is catchable, a fallback returnable, and 'resume next' is gone (PLAT-ERR)"
    elif ! printf '%s' "$old_spelling" | grep -q 'expecting GOTO or STOP'; then
        regressed "CONTROL: 'on error resume next' parses again"
    else
        regressed "CONTROL: a raise is no longer catchable"
    fi
}

control_call_result_compare() {
    # PLAT-BRACE: `call(args) = value` is an ordinary comparison, not a clause.
    printf 'function f(x)\n  return x * 2\nend function\nif f(3) = 6 then\n  print "compared"\nend if\n' > "$WORK/c.bas"
    if run "$WORK/c.bas" | grep -q 'compared'; then
        ok "CONTROL: call(args) = value is a comparison (PLAT-BRACE)"
    else
        regressed "CONTROL: call(args) = value no longer parses as a comparison"
    fi
}

control_keyword_after_dot() {
    # Every keyword must be reachable after a dot, not just a convenient one.
    local kws="as from to end next if then or and not for while until step in loop"
    local bad=""
    for kw in $kws; do
        printf 'r = { %s: 7 }\nprint r.%s\n' "$kw" "$kw" > "$WORK/c.bas"
        if [ "$(run "$WORK/c.bas")" != "7" ]; then
            bad="$bad $kw"
        fi
    done
    if [ -z "$bad" ]; then
        ok "CONTROL: all 16 keywords resolve after a dot"
    else
        regressed "CONTROL: keywords no longer resolve after a dot:$bad"
    fi
}

control_sentinel_find() {
    # PLAT-EQ: find/contains hit on both sentinels.
    cat > "$WORK/c.bas" <<'EOF'
a = [1, nothing, 3]
b = [1, unknown, 3]
print string(find(a, nothing)) + "," + string(find(b, unknown))
print string(contains(a, nothing)) + "," + string(contains(b, unknown))
EOF
    out=$(run "$WORK/c.bas")
    if [ "$(printf '%s' "$out" | head -1)" = "1,1" ] \
       && [ "$(printf '%s' "$out" | tail -1)" = "true,true" ]; then
        ok "CONTROL: find/contains hit on nothing and unknown (PLAT-EQ)"
    else
        regressed "CONTROL: find/contains miss the sentinels again (got: $out)"
    fi
}

control_line_continuation() {
    # PLAT-CONT: a newline inside an unclosed bracket continues the statement.
    printf 'a = [1, 2,\n     3, 4]\nprint count(a)\nprint (1\n       + 2)\n' > "$WORK/c.bas"
    out=$(run "$WORK/c.bas")
    if [ "$(printf '%s' "$out" | head -1)" = "4" ] && [ "$(printf '%s' "$out" | tail -1)" = "3" ]; then
        ok "CONTROL: a line break inside brackets continues the statement (PLAT-CONT)"
    else
        regressed "CONTROL: line continuation is gone (got: $out)"
    fi
}

while IFS='|' read -r key marker; do
    [ -z "$key" ] && continue
    "control_$key"
done <<< "$(printf '%s\n' "$CONTROLS" | sed '/^$/d')"

# --- COVER -------------------------------------------------------------------

section() {
    awk -v s="$SECTION_START" -v e="$SECTION_END" '
        $0 ~ s { inside = 1; next }
        $0 ~ e { inside = 0 }
        inside { print }
    ' "$LEDGER"
}

# One logical bullet per record: a line starting "- " plus its indented tail.
bullets() {
    section | awk '
        /^- / { if (buf != "") print buf; buf = $0; next }
        /^  +[^ ]/ { if (buf != "") buf = buf " " $0; next }
        { if (buf != "") { print buf; buf = "" } }
        END { if (buf != "") print buf }
    ' | sed 's/  */ /g'
}

live_bullets=$(bullets | grep -v '^- ~~')
struck_bullets=$(bullets | grep '^- ~~')

live_n=$(printf '%s\n' "$live_bullets" | sed '/^$/d' | wc -l)
struck_n=$(printf '%s\n' "$struck_bullets" | sed '/^$/d' | wc -l)

if [ "$live_n" -eq 0 ]; then
    fail "COVER: no live bullets parsed out of $LEDGER -- the section markers moved"
fi

# Every probe must find exactly one LIVE bullet.
claimed="$WORK/claimed"
: > "$claimed"
while IFS='|' read -r key marker; do
    [ -z "$key" ] && continue
    n=$(printf '%s\n' "$live_bullets" | grep -cF -- "$marker")
    if [ "$n" = 1 ]; then
        printf '%s\n' "$(printf '%s\n' "$live_bullets" | grep -F -- "$marker")" >> "$claimed"
    elif [ "$n" = 0 ]; then
        fail "COVER: probe $key matches no live bullet (marker: $marker)"
    else
        fail "COVER: probe $key matches $n live bullets (marker: $marker)"
    fi
done <<< "$(printf '%s\n' "$PROBES" | sed '/^$/d')"

while IFS='|' read -r nmkey marker reason; do
    [ -z "$nmkey" ] && continue
    n=$(printf '%s\n' "$live_bullets" | grep -cF -- "$marker")
    if [ "$n" = 1 ]; then
        printf '%s\n' "$(printf '%s\n' "$live_bullets" | grep -F -- "$marker")" >> "$claimed"
        printf 'PASS not mechanical: %s (%s)\n' "$marker" "$reason"
        pass=$((pass + 1))
    else
        fail "COVER: not-mechanical entry $nmkey matches $n live bullets (marker: $marker)"
    fi
done <<< "$(printf '%s\n' "$NOT_MECHANICAL" | sed '/^$/d')"

# And every live bullet must have been claimed by one or the other.
unclaimed=0
while IFS= read -r b; do
    [ -z "$b" ] && continue
    if ! grep -qxF -- "$b" "$claimed"; then
        unclaimed=$((unclaimed + 1))
        printf 'FAIL COVER: live bullet with no probe:\n     %s\n' "$(printf '%s' "$b" | cut -c1-100)"
        status=1
    fi
done <<< "$live_bullets"
if [ "$unclaimed" = 0 ]; then
    ok "COVER: all $live_n live bullets are probed or named not-mechanical"
fi

# --- COVER, second half: struck bullets ---------------------------------------
# A resolved entry must NOT keep a live probe asserting the old behaviour, and
# it MUST have a control proving it really is resolved. Both directions,
# because a struck bullet with nothing behind it is just a deleted bullet.
bad_struck=0
while IFS='|' read -r key marker; do
    [ -z "$key" ] && continue
    if printf '%s\n' "$struck_bullets" | grep -qF -- "$marker"; then
        fail "COVER: probe $key guards a bullet that is already struck through"
        bad_struck=$((bad_struck + 1))
    fi
done <<< "$(printf '%s\n' "$PROBES" | sed '/^$/d')"

ctl_claimed="$WORK/ctl_claimed"
: > "$ctl_claimed"
while IFS='|' read -r key marker; do
    [ -z "$key" ] && continue
    n=$(printf '%s\n' "$struck_bullets" | grep -cF -- "$marker")
    if [ "$n" = 1 ]; then
        printf '%s\n' "$(printf '%s\n' "$struck_bullets" | grep -F -- "$marker")" >> "$ctl_claimed"
    else
        fail "COVER: control $key matches $n struck bullets (marker: $marker)"
        bad_struck=$((bad_struck + 1))
    fi
done <<< "$(printf '%s\n' "$CONTROLS" | sed '/^$/d')"

while IFS= read -r b; do
    [ -z "$b" ] && continue
    if ! grep -qxF -- "$b" "$ctl_claimed"; then
        fail "COVER: struck bullet with no control proving it is resolved:"
        printf '     %s\n' "$(printf '%s' "$b" | cut -c1-100)"
        bad_struck=$((bad_struck + 1))
    fi
done <<< "$struck_bullets"

if [ "$bad_struck" = 0 ]; then
    ok "COVER: all $struck_n struck bullets have a control and no live probe"
fi

printf '\nrun_limitations: PASS=%d SKIP=%d\n' "$pass" "$skip"
exit "$status"
