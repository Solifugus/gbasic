#!/usr/bin/env bash
# `read_at(f, position, count)` -- the other half of a door that only opened
# one way.
#
# `overwrite(f, text, position)` has written at a byte offset since it shipped
# and NOTHING COULD READ AT ONE: a program could modify the middle of a file it
# had no way to inspect, and the only reader was whole-file. That asymmetry is
# the argument for the builtin, and it is why the fixture's load-bearing tier is
# a round trip through BOTH -- one offset space, two directions, which is the
# only check that fails if they disagree about where position 3 is.
#
# It is also the gap that made BINARY INPUT UNSTREAMABLE. `for each line in f`
# is the only other bounded reader and a line is unbounded, so a file with no
# newline byte in it is read whole however the loop is written. That half cannot
# be asserted from inside the language -- a program cannot watch its own
# resident size -- so it is measured here, from outside.
#
# Tiers:
#   SEMANTICS  tests/read_at_test.bas -- 31 checks: the values and every edge a
#              reader meets, the round trip with `overwrite` at all ten
#              positions, that the offsets are BYTES (with the codepoint route
#              as the control), NUL-bearing chunks, and 8 refusals beside the
#              call that must still work.
#   BINARY     the case the streaming loop cannot do, as a DIFFERENCE on one
#              file: chunked reads hold ~15 MB where `for each line` holds the
#              whole thing, and the chunks must reassemble byte-identically.
#   SHAPE      the memory claim: constant, not proportional. Asserted across a
#              4x step in FILE SIZE, which is the only way to tell a bounded
#              reader from one that happens to fit.
#   REOPEN     the cost this builtin HAS, measured rather than hidden: it is
#              positional and stateless, so it opens the file per call. The tier
#              states the figure so the advice in the reference is a number.
#   VALGRIND   a new allocation path per call, bounded by the file.
#
# THE FIXTURE TAKES A SECOND ARGUMENT -- a directory on the REPOSITORY'S
# filesystem -- and that is load-bearing rather than tidy. What `fseek` and
# `ftell` answer for a directory is a fact about the filesystem: on this
# machine's ext4 `ftell` says LONG_MAX, and under /tmp (tmpfs) `fseek` fails.
# The tier that proves the directory guard stops a huge ALLOCATION needs the
# LONG_MAX answer, so pointing it at the `mktemp -d` work directory makes it
# measure nothing -- measured, it stayed GREEN for an hour against a build that
# core-dumps by hand on the same machine.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
. "$(dirname "$0")/premise.sh"   # what this machine can demonstrate
make >/dev/null

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

printf 'TIER semantics: the values, the round trip, the byte offsets, the refusals\n'
: > "$work/empty.txt"
if ! timeout -k 5 60 ./gbasic tests/read_at_test.bas "$work" tests >"$work/out" 2>"$work/err"; then
    cat "$work/err"; fail "the fixture did not run"
elif grep -q 'MISMATCH\|DISAGREE' "$work/out"; then
    grep 'MISMATCH\|DISAGREE' "$work/out"; fail "the fixture disagreed with itself"
elif ! grep -qx 'mismatches: 0' "$work/out"; then
    fail "the fixture did not finish"
else
    n=$(sed -n 's/^checks: //p' "$work/out")
    if [ -z "$n" ] || [ "$n" -lt 28 ]; then
        fail "only ${n:-0} checks ran, wanted at least 28"
    else
        pass "$n checks"
    fi
fi

# One reader, two strategies, chosen by an argument -- so the comparison cannot
# drift onto different work.
cat > "$work/reader.bas" <<'BAS'
program main(args)
    f {file}= args[0]
    how = args[1]
    if how = "chunked" then
        pos = 0
        n = 0
        h = 0
        while true
            c = read_at(f, pos, 65536)
            if c = "" then
                break
            end if
            n = n + 1
            h = h + byte_at(c, 0) + byte_count(c)
            pos = pos + byte_count(c)
        end while
        print "read " + string(pos) + " in " + string(n) + " chunks, mix " + string(h)
    else
        n = 0
        total = 0
        for each line in f
            n = n + 1
            total = total + byte_count(line)
        end for
        print "read " + string(total) + " in " + string(n) + " lines"
    end if
end program
BAS

printf 'TIER binary: the file the streaming loop reads whole however you write it\n'
if ! premise_rss_meter; then
    # NAME THE CLAIM THAT WENT UNMEASURED. A program cannot watch its own peak
    # resident size, so without a meter outside it the headline figure -- 14.8 MB
    # chunked against 115 MB by line -- is not checked by anything.
    pass "SKIP: /usr/bin/time is absent, so the BOUNDED-MEMORY claim was NOT measured (the reassembly below still is)"
fi
if premise_rss_meter; then
# 50 MB with no 0x0a in it. The line loop's `break` fires only AFTER a line is
# materialised, so there is no way to bound it from inside the language.
head -c 52428800 /dev/zero > "$work/zeros.bin"
set +e
c_out=$(/usr/bin/time -f '%M' ./gbasic "$work/reader.bas" "$work/zeros.bin" chunked 2>"$work/c.rss"); c_rc=$?
l_out=$(/usr/bin/time -f '%M' ./gbasic "$work/reader.bas" "$work/zeros.bin" lines 2>"$work/l.rss"); l_rc=$?
set -e
c_rss=$(tail -1 "$work/c.rss"); l_rss=$(tail -1 "$work/l.rss")
if [ "$c_rc" != "0" ] || [ "$l_rc" != "0" ]; then
    cat "$work/c.rss" "$work/l.rss"; fail "one of the two readers did not run"
elif [ "$c_out" != "read 52428800 in 800 chunks, mix 52428800" ]; then
    printf '%s\n' "$c_out"; fail "the chunked reader did not read the file as expected"
elif [ "$l_out" != "read 52428800 in 1 lines" ]; then
    printf '%s\n' "$l_out"
    fail "the line reader did not report ONE line -- the premise of this tier is gone"
else
    ratio10=$(( l_rss * 10 / c_rss ))
    if [ "$ratio10" -ge 30 ]; then
        pass "the same 50 MB: ${c_rss} KB chunked against ${l_rss} KB by line -- $(( ratio10 / 10 )).$(( ratio10 % 10 ))x"
    else
        fail "${c_rss} KB chunked against ${l_rss} KB by line -- only $(( ratio10 / 10 )).$(( ratio10 % 10 ))x, so the chunked read is not bounded"
    fi
fi
fi   # premise_rss_meter
# AND IT MUST REASSEMBLE, WITH OR WITHOUT A METER. "Bounded" is worthless if the
# bytes are wrong, and a
# 50 MB file is where an off-by-one at a chunk boundary would hide.
cat > "$work/rebuild.bas" <<'BAS'
program main(args)
    f {file}= args[0]
    out {file}= args[1]
    write(out, "")
    pos = 0
    while true
        c = read_at(f, pos, 65536)
        if c = "" then
            break
        end if
        append(out, c)
        pos = pos + byte_count(c)
    end while
    print "wrote " + string(bytes(out))
end program
BAS
printf 'head\n' > "$work/mixed.bin"
head -c 3000000 /dev/urandom >> "$work/mixed.bin"
printf 'tail' >> "$work/mixed.bin"
./gbasic "$work/rebuild.bas" "$work/mixed.bin" "$work/rebuilt.bin" >/dev/null
if cmp -s "$work/mixed.bin" "$work/rebuilt.bin"; then
    pass "3 MB of random bytes, chunked and reassembled, compare byte-identical"
else
    fail "the reassembled file differs -- a chunk boundary loses or repeats bytes"
fi

printf 'TIER shape: the memory is constant in the FILE, not proportional\n'
if ! premise_rss_meter; then
    pass "SKIP: /usr/bin/time is absent, so CONSTANT-IN-THE-FILE was NOT measured -- which is the only way to tell a bounded reader from one that happens to fit"
else
# The claim is bounded, and the only way to tell a bounded reader from one that
# merely fits is to grow the file and watch the reader stay still. 4x step,
# asserted as a RATIO, never an absolute size.
head -c 33554432 /dev/zero > "$work/small.bin"
head -c 134217728 /dev/zero > "$work/large.bin"
s_rss=0; l2_rss=0
for which in small large; do
    /usr/bin/time -f '%M' ./gbasic "$work/reader.bas" "$work/$which.bin" chunked \
        >/dev/null 2>"$work/$which.rss"
    r=$(tail -1 "$work/$which.rss")
    if [ "$which" = "small" ]; then s_rss=$r; else l2_rss=$r; fi
done
ratio10=$(( l2_rss * 10 / s_rss ))
if [ "$ratio10" -le 13 ]; then
    pass "32 MB reads in ${s_rss} KB and 128 MB in ${l2_rss} KB -- $(( ratio10 / 10 )).$(( ratio10 % 10 ))x over a 4x file"
else
    fail "32 MB reads in ${s_rss} KB and 128 MB in ${l2_rss} KB -- $(( ratio10 / 10 )).$(( ratio10 % 10 ))x, so the reader grows with the file"
fi
fi   # premise_rss_meter

printf 'TIER reopen: the cost this builtin has, stated as a number\n'
# `read_at` is POSITIONAL AND STATELESS, so it opens the file on every call --
# which is what makes it safe to interleave with anything else touching that
# file, and what makes a small chunk size expensive. Measured rather than
# hidden, because the reference gives advice and advice should carry a figure.
t_small=0; t_big=0
for n in 4096 65536; do
    start=$(date +%s%N)
    ./gbasic "$work/cost.bas" >/dev/null 2>&1 || true
    cat > "$work/cost.bas" <<'BAS'
program main(args)
    f {file}= args[0]
    n = number(args[1])
    pos = 0
    while true
        c = read_at(f, pos, n)
        if c = "" then
            break
        end if
        pos = pos + byte_count(c)
    end while
    print string(pos)
end program
BAS
    start=$(date +%s%N)
    ./gbasic "$work/cost.bas" "$work/small.bin" "$n" >/dev/null
    end=$(date +%s%N)
    ms=$(( (end - start) / 1000000 ))
    if [ "$n" = "4096" ]; then t_small=$ms; else t_big=$ms; fi
done
calls_small=$(( 33554432 / 4096 ))
if [ "$t_small" -lt 10 ]; then
    fail "32 MB in 4 KB chunks took only ${t_small}ms -- too fast to state a per-call cost from"
else
    pass "32 MB: ${t_small}ms in $calls_small calls of 4 KB against ${t_big}ms in 512 of 64 KB -- the reopen is real and small"
fi

printf 'TIER valgrind\n'
if vg_available; then
    rm -rf "$work/vg"; mkdir -p "$work/vg"; : > "$work/vg/empty.txt"
    if vg_run ./gbasic tests/read_at_test.bas "$work/vg" tests >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        tail -30 "$work/vg.err"; fail "valgrind objected"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

exit $status
