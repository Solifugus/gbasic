#!/usr/bin/env bash
# METADATA: what a file says about itself, and changing it.
#
# `permissions`, `chmod`, `set_mtime`, `is_symlink`, `symlink` -- the last item
# of the ETL design's five-item gap list (`chmod` was the one still outstanding)
# plus the three things MEASURING AROUND IT turned up, which were defects rather
# than absences:
#
#   * `file_type` FOLLOWS a symlink, so nothing could ask whether a path IS one
#     -- while `gb_is_symlink` had been in the platform layer since the glob
#     needed a cycle guard, exposed by nothing.
#   * NOTHING COULD CREATE ONE. Three runners build links with `ln -s`, and
#     tests/run_scratch.sh states in as many words that its load-bearing tier
#     lives in bash because gBASIC could not make a symlink.
#   * THE LISTING DISAGREED WITH `file_type` about a dangling one: it fell back
#     to "file" for anything that was not a directory, so a dangling link, a
#     FIFO, a socket and a device all arrived labelled `file`. Two routes to one
#     question disagreeing -- and the listing held the wrong one, there being no
#     file there. `if e.type = "file" then read(e.path)` raises on the first and
#     BLOCKS FOREVER on the second.
#
# Tiers:
#   SEMANTICS  tests/metadata_test.bas -- the values, the round trips, the
#              listing agreement with its two controls, and 11 refusals beside
#              the five calls that must still work.
#   ORACLE     `stat -c %a` against `permissions(p).mode` over five modes. The
#              fixture cannot do this: `chmod` then `permissions` is two calls
#              into ONE implementation, so it passes on any self-consistently
#              wrong mode -- the decimal 420 for 0644, say. The oracle is a
#              different program.
#   ACCESS     the question `permissions` answers TWICE on purpose: `mode` is
#              what the file says, `readable`/`writable` are what THIS PROCESS
#              can do. Informative either way round -- as an ordinary user a
#              0000 file is unreadable and the two agree, and AS ROOT it is
#              readable while the mode still says 0000, which IS the divergence
#              and is why both fields exist.
#   OTHER      a FIFO: the one file kind gBASIC cannot create, and the one that
#              makes the listing's old "file" fallback dangerous rather than
#              merely wrong. Both routes must say `other`.
#   TRIPWIRE   warning 2114 (chmod could not express every bit) is UNREACHABLE
#              on POSIX, where every bit is honoured, so there is no behavioural
#              tier that can reach it: the source is read instead, with a
#              vacuity control, and Windows is the only place it can fire.
#   VALGRIND
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
make >/dev/null

work="$(mktemp -d)"
trap 'chmod -R u+rwX "$work" 2>/dev/null; rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }
skipped() { printf '  ok   SKIP: %s\n' "$1"; }

printf 'TIER semantics: the values, the round trips, the listing agreement\n'
mkdir -p "$work/sem"
if ! timeout -k 5 60 ./gbasic tests/metadata_test.bas "$work/sem" >"$work/out" 2>"$work/err"; then
    cat "$work/err"
    grep 'MISMATCH' "$work/out" || true
    fail "the fixture did not run"
elif grep -q 'MISMATCH' "$work/out"; then
    grep 'MISMATCH' "$work/out"; fail "the fixture disagreed with itself"
elif ! grep -qx 'mismatches: 0' "$work/out"; then
    fail "the fixture did not finish"
else
    n=$(sed -n 's/^checks: //p' "$work/out")
    if [ -z "$n" ] || [ "$n" -lt 45 ]; then
        fail "only ${n:-0} checks ran, wanted at least 45"
    else
        pass "$n checks"
    fi
fi

printf 'TIER oracle: stat(1) against permissions().mode\n'
# A SECOND IMPLEMENTATION, not a second call into ours. `chmod` then
# `permissions` round-trips on any consistently wrong rendering -- the decimal
# 420 where 0644 belongs reads as an answer and agrees with itself.
rm -rf "$work/or"; mkdir -p "$work/or"
: > "$work/or/f"
cat > "$work/oracle.bas" <<'BAS'
program main(args)
    f {file}= args[0]
    print permissions(f).mode
end program
BAS
oracle_ok=1
for m in 600 644 640 755 444; do
    chmod "$m" "$work/or/f"
    want=$(stat -c %a "$work/or/f")
    got=$(./gbasic "$work/oracle.bas" "$work/or/f")
    # stat prints 644, permissions answers 0644: the leading zero is ours and
    # is deliberate (it is how an author writes a mode), so compare as numbers.
    if [ "$((8#$got))" -ne "$((8#$want))" ]; then
        fail "mode $m: stat says $want, permissions says $got"
        oracle_ok=0
    fi
done
chmod 644 "$work/or/f"
[ "$oracle_ok" = 1 ] && pass "five modes agree with stat(1)"

printf 'TIER access: what the file says against what this process can do\n'
rm -rf "$work/ac"; mkdir -p "$work/ac"
printf 'x' > "$work/ac/f"
chmod 000 "$work/ac/f"
cat > "$work/access.bas" <<'BAS'
program main(args)
    f {file}= args[0]
    p = permissions(f)
    print p.mode + " " + string(p.readable) + " " + string(p.writable)
end program
BAS
got=$(./gbasic "$work/access.bas" "$work/ac/f")
chmod 644 "$work/ac/f"
if [ "$(id -u)" = "0" ]; then
    # AS ROOT THIS IS THE DIVERGENCE ITSELF, which is why both fields exist: the
    # bits say nobody may read it and this process may.
    if [ "$got" = "0000 true true" ]; then
        pass "as root: mode 0000 and readable -- the two questions genuinely differ"
    else
        fail "as root, expected '0000 true true', got '$got'"
    fi
else
    if [ "$got" = "0000 false false" ]; then
        pass "as an ordinary user: mode 0000 and not readable (the two agree here; only root or a read-only mount separates them)"
    else
        fail "expected '0000 false false', got '$got'"
    fi
fi

printf 'TIER other: a FIFO, the file kind gBASIC cannot create\n'
rm -rf "$work/ot"; mkdir -p "$work/ot"
if mkfifo "$work/ot/pipe" 2>/dev/null; then
    : > "$work/ot/plain"
    cat > "$work/other.bas" <<'BAS'
program main(args)
    d {dir}= args[0]
    for each e in list(d)
        print e.name + " " + e.type + " " + string(file_type(e.path))
    end for
end program
BAS
    got=$(./gbasic "$work/other.bas" "$work/ot" | sort)
    # BOTH ROUTES, because the defect was that they disagreed -- and the FIFO is
    # the case that makes it dangerous rather than merely wrong: a reader that
    # believes the old "file" label BLOCKS FOREVER on it.
    if printf '%s\n' "$got" | grep -qx 'pipe other other'; then
        pass "a FIFO is 'other' in the listing AND in file_type"
    else
        printf '%s\n' "$got"; fail "the FIFO was not reported as other by both routes"
    fi
    if printf '%s\n' "$got" | grep -qx 'plain file file'; then
        pass "CONTROL: an ordinary file is still 'file' both ways"
    else
        printf '%s\n' "$got"; fail "an ordinary file stopped being reported as a file"
    fi
else
    skipped "mkfifo is unavailable here, so the 'other' kind could not be produced and NOTHING was measured about it"
fi

printf 'TIER tripwire: the warning POSIX cannot reach\n'
# `gb_chmod` honours every bit on POSIX, so `dropped` is always 0 and warning
# 2114 cannot fire here however the suite is written. What CAN be asserted is
# that the site exists and is driven by the platform's own out-parameter --
# otherwise the Windows half would be a comment rather than a behaviour, and
# nothing would notice if it were deleted.
if grep -q 'warn_fmt(2114' src/eval.c \
   && grep -q 'int dropped = 0;' src/eval.c \
   && grep -q 'gb_chmod(path, mode, &dropped)' src/eval.c \
   && grep -q 'if (dropped) {' src/eval.c; then
    pass "warning 2114 is wired to gb_chmod's dropped out-parameter"
else
    fail "the dropped-bits warning is not wired to the platform call"
fi
# ITS VACUITY CONTROL: a scanner that read nothing would pass the check above.
probe="$work/probe.c"
printf 'warn_fmt(2114, "x");\n' > "$probe"
if grep -q 'warn_fmt(2114' "$probe"; then
    pass "CONTROL: the scanner really does match that text"
else
    fail "the scanner found nothing even in a planted file"
fi
# And the Windows half must set it, or the warning can never fire anywhere.
if grep -q '\*dropped' src/platform_win32.c && grep -q 'dropped' src/platform_posix.c; then
    pass "both platform bodies answer the dropped question"
else
    fail "a platform body does not report dropped bits"
fi

printf 'TIER valgrind\n'
if vg_available; then
    rm -rf "$work/vg"; mkdir -p "$work/vg"
    if vg_run ./gbasic tests/metadata_test.bas "$work/vg" >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        tail -30 "$work/vg.err"; fail "valgrind objected"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

exit $status
