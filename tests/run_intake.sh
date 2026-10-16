#!/usr/bin/env bash
# `intake` increment 1 -- what arrived, what was taken (docs/intake_design.md).
#
# The half of the ETL cycle that needs STATE. Everything else that brief asked
# for became a primitive, because it was a thing the language could not do;
# "have I already imported this?" is different in kind -- a program could always
# answer it, and had to write the answer itself, every time, differently.
#
# SELF-CHECKING NOT GOLDEN, AND FORCED: every defect this library can have
# produces AN ORDINARY-LOOKING SUCCESSFUL RUN. A ledger that records nothing, an
# identity that always matches, a readiness rule that always says yes -- each
# prints the transcript of a working import. A golden would record one as
# expected and defend it, and the thing being got wrong is precisely the thing
# nobody can see: whether a file was imported twice or not at all.
#
# Tiers, and which of them the fixture CANNOT hold:
#   SEMANTICS    tests/intake_test.bas -- 44 checks: the round trip, per-file
#                durability, the dotted ledger inside the directory it scans,
#                sentinel readiness, and 12 refusals each beside the call that
#                must still work.
#   IDENTITY     THE LOAD-BEARING TIER, and a THREE-WAY DIFFERENCE. The same
#                filename re-dropped: with new contents AND a new size, "path"
#                must skip while "stamp" and "content" take; with the SAME size
#                and the SAME mtime, "stamp" must skip while "content" still
#                takes. Either case alone passes on a ledger that always answers
#                one way, and the second needs `touch -r`, which is why it is
#                here and not in the fixture.
#   STABLE_FOR   the window, without a clock: a backdated file is ready and a
#                fresh one is not, asserted as a DIFFERENCE in one run. The
#                design's own measurement is that the window is a RACE, so what
#                is testable is the rule, not the safety.
#   VANISHED     a dangling symlink is the vanished file FROZEN -- measured, the
#                listing reports it (type=file, size=unknown) and `exists`
#                answers false, which is why the check is a QUESTION rather than
#                a caught raise. It must warn, skip, and the scan must still
#                take the file beside it.
#   CRASH        SIGKILL mid-import. Durability is a claim about what survives a
#                death, so the tier performs one: a second `open` must report
#                EXACTLY the files that completed -- not fewer (the journal was
#                not durable) and not more (it recorded work that never ran).
#   CONCURRENCY  two runs at once. Measured, `lock` blocks and has no
#                non-blocking form, so the claim is "one WAITS" and the tier
#                measures the wait rather than asserting a refusal there is no
#                way to make.
#   COMPACTION   a journal folded into the snapshot leaves the same answer, and
#                the snapshot is written BEFORE the journal is removed.
#   VALGRIND
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -euo pipefail

cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh
make >/dev/null
export GBASIC_PATH=stdlib
export TZ=UTC

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
status=0
pass() { printf '  ok   %s\n' "$1"; }
fail() { printf '  FAIL %s\n' "$1"; status=1; }

# One scanner used by most tiers: open with a declared identity and readiness,
# take everything that arrived, print the count.
# NOTE the ledger's NAME and the identity RULE are separate arguments. The
# first draft derived one from the other, which cost two tiers: the compaction
# tier asked for a ledger called "split" and was refused as an unknown identity,
# and the concurrency tier -- which discarded the scanner's output with
# `|| true` -- read that same refusal as "the lock does not block", reporting a
# missing lock over a lock that works. The reports-the-wrong-cause class, from
# inside a tier written to measure something else.
cat > "$work/scan.bas" <<'BAS'
load intake
program main(args)
    root = args[0]
    name = args[1]
    ident = args[2]
    ready = args[3]
    led = intake.open({file}(root + "/." + name + ".json"),
                      { identity: ident, ready: ready })
    n = 0
    for each f in intake.arrived(led, {dir}root, "**/*.csv")
        n = n + 1
        led = intake.taken(led, f, { seq: n })
    end for
    print "took " + string(n)
    intake.close(led)
end program
BAS

printf 'TIER semantics: the round trip, durability, and the refusals\n'
if ! timeout -k 5 120 ./gbasic tests/intake_test.bas "$work/fx" >"$work/out" 2>"$work/err"; then
    cat "$work/err"; fail "the fixture did not run"
elif grep -q MISMATCH "$work/out"; then
    grep MISMATCH "$work/out"; fail "the fixture disagreed with itself"
elif ! grep -qx 'mismatches: 0' "$work/out"; then
    fail "the fixture did not finish"
else
    n=$(sed -n 's/^checks: //p' "$work/out")
    if [ -z "$n" ] || [ "$n" -lt 40 ]; then
        fail "only ${n:-0} checks ran, wanted at least 40"
    else
        pass "$n checks"
    fi
fi

printf 'TIER identity: a three-way difference over the same re-dropped name\n'
# CASE A -- the re-drop changes the SIZE. "path" is the only rule that should
# skip it.
for mode in path stamp content; do
    d="$work/idA_$mode"; mkdir -p "$d"
    printf 'v1\n' > "$d/daily.csv"
    ./gbasic "$work/scan.bas" "$d" led "$mode" renamed >/dev/null
    printf 'v2-and-longer\n' > "$d/daily.csv"
    got=$(./gbasic "$work/scan.bas" "$d" led "$mode" renamed)
    case "$mode:$got" in
        "path:took 0")    pass "A/path skips a re-drop (the path has not changed)" ;;
        "stamp:took 1")   pass "A/stamp takes it (the size has)" ;;
        "content:took 1") pass "A/content takes it (the bytes have)" ;;
        *) fail "A/$mode answered '$got'" ;;
    esac
done
# CASE B -- the re-drop keeps the size AND the mtime, which is a corrected file
# of the same length. Only "content" can see it, and this is the case that
# separates the two cheap rules from the expensive one.
for mode in stamp content; do
    d="$work/idB_$mode"; mkdir -p "$d"
    printf 'AAAA\n' > "$d/fixed.csv"
    touch -d '2026-03-04 05:06:07' "$d/fixed.csv"
    ./gbasic "$work/scan.bas" "$d" led "$mode" renamed >/dev/null
    printf 'BBBB\n' > "$d/fixed.csv"
    touch -d '2026-03-04 05:06:07' "$d/fixed.csv"
    size_now=$(wc -c < "$d/fixed.csv")
    got=$(./gbasic "$work/scan.bas" "$d" led "$mode" renamed)
    case "$mode:$got" in
        "stamp:took 0")   pass "B/stamp skips it -- same path, same $size_now bytes, same mtime" ;;
        "content:took 1") pass "B/content takes it, which is what the hash is FOR" ;;
        *) fail "B/$mode answered '$got' (size $size_now)" ;;
    esac
done

printf 'TIER stable_for: the window, asserted without a clock\n'
# A backdated file is past any window; a file written now is not. Both in ONE
# run, so the tier is a difference rather than a fact about how fast the machine
# got to the second assertion.
d="$work/stable"; mkdir -p "$d"
printf 'old\n' > "$d/old.csv"
touch -d '2026-01-01 00:00:00' "$d/old.csv"
printf 'new\n' > "$d/new.csv"
cat > "$work/stable.bas" <<'BAS'
load intake
program main(args)
    led = intake.open({file}(args[0] + "/.s.json"),
                      { identity: "path", ready: { stable_for: 1 hour } })
    for each f in intake.arrived(led, {dir}args[0], "**/*.csv")
        print "ready " + f.name
    end for
    intake.close(led)
end program
BAS
out=$(./gbasic "$work/stable.bas" "$d")
if printf '%s\n' "$out" | grep -qx 'ready old.csv'; then
    pass "a file older than the window is ready"
else
    printf '%s\n' "$out"; fail "a backdated file was not ready"
fi
if printf '%s\n' "$out" | grep -qx 'ready new.csv'; then
    printf '%s\n' "$out"; fail "a file written a moment ago was taken as settled"
else
    pass "CONTROL: one written a moment ago is not"
fi

printf 'TIER vanished: a file in the listing that is not there to read\n'
# A dangling symlink is that case FROZEN, which is the only deterministic way
# to write it -- a real mid-scan deletion is a race with another process.
d="$work/vanish"; mkdir -p "$d"
printf 'real\n' > "$d/real.csv"
ln -s /nonexistent/gone.csv "$d/gone.csv"
set +e
out=$(./gbasic "$work/scan.bas" "$d" led content renamed 2>&1); rc=$?
set -e
if [ "$rc" != "0" ]; then
    printf '%s\n' "$out"; fail "the scan died on a dangling entry (exit $rc)"
elif ! printf '%s\n' "$out" | grep -q "vanished during the scan"; then
    printf '%s\n' "$out"; fail "it was skipped in SILENCE -- a withdrawn file must be reported"
elif ! printf '%s\n' "$out" | grep -qx "took 1"; then
    printf '%s\n' "$out"; fail "the scan did not continue to the file beside it"
else
    pass "it warns, skips, and still takes the file beside it"
fi
# The premise the tier rests on, asserted rather than assumed: the LISTING
# reports the entry while `exists` denies it, which is why the check is a value
# question and not a caught raise.
cat > "$work/premise.bas" <<'BAS'
program main(args)
    seen = 0
    for each f in files({dir}args[0], "**/*.csv")
        if f.name = "gone.csv" then
            seen = 1
            print "listing reports it: type=" + f.type + " size=" + string(f.size)
            print "exists says: " + string(exists({file}f.path))
        end if
    end for
    if seen = 0 then
        print "PREMISE LOST: the listing did not report the dangling entry"
    end if
end program
BAS
pout=$(./gbasic "$work/premise.bas" "$d")
if printf '%s\n' "$pout" | grep -q "listing reports it" && printf '%s\n' "$pout" | grep -qx "exists says: false"; then
    pass "PREMISE: the listing reports it and exists() denies it"
else
    printf '%s\n' "$pout"; fail "the premise this tier rests on does not hold"
fi

printf 'TIER crash: a SIGKILL mid-import, and what the ledger says afterwards\n'
# DURABILITY IS A CLAIM ABOUT WHAT SURVIVES A DEATH, so the tier performs one.
# The importer publishes a gate file after N files so the kill lands provably
# mid-run, and is killed with -9: a caught error would prove nothing about a
# half-written ledger.
d="$work/crash"; mkdir -p "$d"
for i in 1 2 3 4 5 6; do printf 'row %d\n' "$i" > "$d/f$i.csv"; done
cat > "$work/crasher.bas" <<'BAS'
load intake
program main(args)
    root = args[0]
    stop_after = number(args[1])
    led = intake.open({file}(root + "/.c.json"),
                      { identity: "path", ready: "renamed" })
    n = 0
    for each f in intake.arrived(led, {dir}root, "**/*.csv")
        led = intake.taken(led, f, { seq: n })
        n = n + 1
        if n = stop_after then
            ' Published only once the work is durable, so the kill below cannot
            ' race the append it is meant to land after.
            gate {file}= root + "/gate"
            write(gate, string(n))
            ' Nothing else to do but wait to be killed.
            sleep(30)
        end if
    end for
    print "finished " + string(n)
    intake.close(led)
end program
BAS
rm -f "$d/gate"
./gbasic "$work/crasher.bas" "$d" 3 >"$work/crash.out" 2>&1 &
cpid=$!
for _ in $(seq 1 100); do [ -f "$d/gate" ] && break; sleep 0.1; done
if [ ! -f "$d/gate" ]; then
    kill -9 $cpid 2>/dev/null || true
    fail "the importer never reached its gate -- nothing was measured"
else
    kill -9 $cpid 2>/dev/null || true
    wait $cpid 2>/dev/null || true
    # The lock is released by the KERNEL when the process dies, which is what
    # makes a killed run survivable rather than a wedged directory.
    cat > "$work/after.bas" <<'BAS'
load intake
program main(args)
    led = intake.open({file}(args[0] + "/.c.json"),
                      { identity: "path", ready: "renamed" })
    print "recorded " + string(intake.count_taken(led))
    rest = intake.arrived(led, {dir}args[0], "**/*.csv")
    print "remaining " + string(count(rest))
    intake.close(led)
end program
BAS
    set +e
    aout=$(timeout -k 5 30 ./gbasic "$work/after.bas" "$d" 2>&1); arc=$?
    set -e
    if [ "$arc" != "0" ]; then
        printf '%s\n' "$aout"; fail "the ledger could not be opened after the kill (exit $arc)"
    elif printf '%s\n' "$aout" | grep -qx "recorded 3" && printf '%s\n' "$aout" | grep -qx "remaining 3"; then
        pass "exactly the 3 that completed survived, and the other 3 are still to do"
    else
        printf '%s\n' "$aout"
        fail "the ledger did not report exactly what completed"
    fi
fi

printf 'TIER concurrency: the second run waits, because it cannot refuse\n'
# `lock` blocks and has no non-blocking form -- measured at 2.91s against a
# 3.0s holder while designing this -- so "one waits or refuses" is only half
# available, and this tier measures the half that is.
d="$work/conc"; mkdir -p "$d"
printf 'x\n' > "$d/a.csv"
cat > "$work/holder.bas" <<'BAS'
load intake
program main(args)
    led = intake.open({file}(args[0] + "/.h.json"),
                      { identity: "path", ready: "renamed" })
    gate {file}= args[0] + "/held"
    write(gate, "1")
    sleep(3.0)
    intake.close(led)
end program
BAS
rm -f "$d/held"
./gbasic "$work/holder.bas" "$d" >/dev/null 2>&1 &
hpid=$!
for _ in $(seq 1 100); do [ -f "$d/held" ] && break; sleep 0.1; done
if [ ! -f "$d/held" ]; then
    kill -9 $hpid 2>/dev/null || true
    fail "the first run never took the lock -- nothing was measured"
else
    start=$(date +%s%N)
    set +e
    cout=$(timeout -k 5 30 ./gbasic "$work/scan.bas" "$d" h path renamed 2>&1); crc=$?
    set -e
    end=$(date +%s%N)
    waited=$(( (end - start) / 100000000 ))   # tenths of a second
    wait $hpid 2>/dev/null || true
    # The output is KEPT and checked first. Discarding it is how the first draft
    # of this tier reported a missing lock over a second run that had merely
    # been refused for an unrelated reason.
    if [ "$crc" != "0" ]; then
        printf '%s\n' "$cout"
        fail "the second run did not complete (exit $crc) -- this tier measured nothing"
    elif [ "$waited" -ge 15 ]; then
        pass "the second run waited $(( waited / 10 )).$(( waited % 10 ))s for the first"
    else
        fail "the second run did not wait (only $(( waited / 10 )).$(( waited % 10 ))s) -- two runs can interleave"
    fi
fi

printf 'TIER compaction: folding the journal changes no answer\n'
d="$work/compact"; mkdir -p "$d"
for i in 1 2 3 4; do printf 'c %d\n' "$i" > "$d/c$i.csv"; done
./gbasic "$work/scan.bas" "$d" path path renamed >/dev/null
if [ -f "$d/.path.json" ] && [ ! -f "$d/.path.json.journal" ]; then
    pass "close left a snapshot and no journal"
else
    ls -la "$d"; fail "the journal was not folded in"
fi
before=$(./gbasic "$work/scan.bas" "$d" path path renamed)
# Now put the SAME four entries back as a journal beside the snapshot and make
# the snapshot stale, which is the state a crashed run leaves: the answer must
# not change.
cat > "$work/split.bas" <<'BAS'
load intake
program main(args)
    ' An empty snapshot plus a journal holding everything -- the worst split
    ' there is, and the one a run killed before its first close produces.
    led = intake.open({file}(args[0] + "/.split.json"),
                      { identity: "path", ready: "renamed" })
    for each f in intake.arrived(led, {dir}args[0], "**/*.csv")
        led = intake.taken(led, f, {})
    end for
    print "took " + string(intake.count_taken(led))
    ' Deliberately NOT closed: no snapshot is written, so everything this run
    ' knows lives in the journal alone.
end program
BAS
./gbasic "$work/split.bas" "$d" >"$work/split.out" 2>&1
again=$(./gbasic "$work/scan.bas" "$d" split path renamed 2>&1 || true)
if [ "$(cat "$work/split.out")" = "took 4" ] && [ "$again" = "took 0" ]; then
    pass "a run that never closed is still recorded: replayed from the journal alone"
else
    printf 'first: %s\nsecond: %s\n' "$(cat "$work/split.out")" "$again"
    fail "a journal-only ledger did not survive"
fi
[ "$before" = "took 0" ] && pass "CONTROL: the compacted ledger still answers 'nothing new'" \
    || fail "the compacted ledger lost its entries ($before)"

printf 'TIER ordering: a failed snapshot write must not lose the journal\n'
# `compact` writes the snapshot and THEN removes the journal, and the comment
# saying why was for two hours the only thing asserting it: a perturbation that
# swapped the two lines left every other tier GREEN. The claim is only testable
# if a durable write can be made to FAIL on purpose, and the way to do that is
# to put a directory where the snapshot has to go -- after `open`, since `open`
# now refuses a ledger it cannot read.
d="$work/ordering"; mkdir -p "$d/data"
for i in 1 2 3; do printf 'x%d\n' "$i" > "$d/data/f$i.csv"; done
cat > "$work/ordering.bas" <<'BAS'
load intake
program main(args)
    root = args[0]
    led = intake.open({file}(root + "/led.json"),
                      { identity: "path", ready: "renamed" })
    for each f in intake.arrived(led, {dir}(root + "/data"), "*.csv")
        led = intake.taken(led, f, {})
    end for
    print "took " + string(intake.count_taken(led))
    make_dir(root + "/led.json")
    on error goto next
    intake.close(led)
    if error then
        print "close raised"
        error.clear()
    else
        print "close SUCCEEDED"
    end if
end program
BAS
oout=$(./gbasic "$work/ordering.bas" "$d" 2>&1 || true)
if ! printf '%s\n' "$oout" | grep -qx "took 3"; then
    printf '%s\n' "$oout"; fail "the ordering tier did not get as far as taking its files"
elif ! printf '%s\n' "$oout" | grep -qx "close raised"; then
    printf '%s\n' "$oout"; fail "close did not fail, so this tier measured nothing"
elif [ -f "$d/led.json.journal" ] && [ "$(wc -l < "$d/led.json.journal")" = "3" ]; then
    pass "the write failed and all 3 entries are still in the journal"
else
    fail "the journal was removed before the snapshot was durable -- 3 entries lost"
fi

printf 'TIER a directory where a file should be\n'
# FOUND BY THIS LIBRARY AND FIXED IN THE RUNTIME: `read` on a directory used to
# ABORT the interpreter (fopen succeeds, ftell answers LONG_MAX, malloc fails,
# abort) -- SIGABRT and a core dump, which takes block-buffered stdout with it,
# so a program tracing its progress printed nothing at all. `persist.read_status`
# then raised out of the one function whose contract is that it never does.
cat > "$work/dirread.bas" <<'BAS'
load persist
program main(args)
    on error goto next
    t = read({file}args[0])
    if error then
        print "read refused: " + error.message
        error.clear()
    else
        print "read ANSWERED " + string(len(t)) + " bytes for a directory"
    end if
    st = persist.read_status(args[0])
    if error then
        print "read_status RAISED, which its contract forbids"
        error.clear()
    else
        print "read_status: " + st.status
    end if
end program
BAS
mkdir -p "$d/adirectory"
dout=$(./gbasic "$work/dirread.bas" "$d/adirectory" 2>&1 || true)
if printf '%s\n' "$dout" | grep -q "read refused.*Is a directory"; then
    pass "read names it rather than aborting the process"
else
    printf '%s\n' "$dout"; fail "read on a directory did not refuse by name"
fi
if printf '%s\n' "$dout" | grep -qx "read_status: corrupt"; then
    pass "persist.read_status keeps its promise and reports it as a value"
else
    printf '%s\n' "$dout"; fail "persist.read_status did not report an unreadable store as a value"
fi
# THE CONTROL: an ordinary file still reads, so neither fix is "refuse things".
printf 'hello\n' > "$d/ordinary.txt"
cout2=$(./gbasic "$work/dirread.bas" "$d/ordinary.txt" 2>&1 || true)
if printf '%s\n' "$cout2" | grep -q "read ANSWERED 6 bytes"; then
    pass "CONTROL: an ordinary file still reads, and its status is still reported"
else
    printf '%s\n' "$cout2"; fail "an ordinary file stopped reading"
fi

printf 'TIER shape: the journal against the snapshot rewrite it replaced\n'
# THE MEASUREMENT THAT CHOSE THE DESIGN, asserted as a DIFFERENCE BETWEEN TWO
# IMPLEMENTATIONS rather than as a property of one -- because the first two
# drafts of this tier both asserted something FALSE and the numbers said so.
#
# Draft 1 timed whole runs and reported 4.6x over a 10x ledger, which is true
# and is not the claim: `open` parses the snapshot and `close` writes it, so the
# per-RUN cost is legitimately linear in what the ledger holds.
#
# Draft 2 subtracted that baseline and reported 10.9x -- so the per-FILE cost is
# linear too, and the design doc's "it costs nothing" was WRONG. Isolated with
# no intake involved: 120 record-field assignments THROUGH A FUNCTION cost ~0s
# at 2,000 fields and ~1s at 20,000, because a gBASIC record is a VALUE and
# writing one field of a copy detaches the whole field array. The alternative
# that would make it flat -- keeping the map in library-level state -- DOES NOT
# EXIST IN THE LANGUAGE: a variable assigned in one library function is not
# visible from another (`undefined variable: _state`, measured).
#
# So what the journal actually buys is measured HERE against the design it
# replaced, implemented in the tier as a second implementation: the same files,
# the same ledger, with `persist.write_atomic` of the whole snapshot after each
# one. The gate is 5x, and the measurement is far wider than that.
cat > "$work/grow.bas" <<'BAS'
load intake
program main(args)
    root = args[0]
    seed = number(args[1])
    name = args[2]
    led = intake.open({file}(root + "/" + name + ".json"),
                      { identity: "path", ready: "renamed" })
    i = 0
    while i < seed
        led = intake.taken(led, { path: "/seed/" + string(i) + ".csv",
                                  identity: "path:/seed/" + string(i) + ".csv",
                                  relative: string(i) + ".csv", size: 1,
                                  modified: unknown }, {})
        i = i + 1
    end while
    intake.close(led)
    print "seeded " + string(seed)
end program
BAS
# ONE program, two durability strategies, chosen by an argument -- so the
# comparison cannot drift onto different work.
cat > "$work/strategy.bas" <<'BAS'
load intake
load persist
program main(args)
    root = args[0]
    name = args[1]
    how = args[2]
    led = intake.open({file}(root + "/" + name + ".json"),
                      { identity: "path", ready: "renamed" })
    n = 0
    for each f in intake.arrived(led, {dir}(root + "/files"), "*.csv")
        led = intake.taken(led, f, {})
        if how = "rewrite" then
            ' THE DESIGN THE JOURNAL REPLACED: per-file durability by rewriting
            ' the whole snapshot. Same guarantee, and this is what it costs.
            persist.write_atomic(root + "/" + name + ".rewrite.json",
                                 { version: 1, identity: "path",
                                   taken: led.taken })
        end if
        n = n + 1
    end for
    print "took " + string(n)
end program
BAS
d="$work/shape"; mkdir -p "$d/files"
for i in $(seq 1 120); do printf 'r\n' > "$d/files/t$i.csv"; done
./gbasic "$work/grow.bas" "$d" 20000 jnl >/dev/null
cp "$d/jnl.json" "$d/rwr.json"
t_j=0; t_r=0; took_j=""; took_r=""
for how in journal rewrite; do
    name=jnl; [ "$how" = "rewrite" ] && name=rwr
    start=$(date +%s%N)
    took=$(./gbasic "$work/strategy.bas" "$d" "$name" "$how")
    end=$(date +%s%N)
    ms=$(( (end - start) / 1000000 ))
    if [ "$how" = "journal" ]; then t_j=$ms; took_j=$took; else t_r=$ms; took_r=$took; fi
done
if [ "$took_j" != "took 120" ] || [ "$took_r" != "took 120" ]; then
    fail "the shape tier did not take its 120 files ($took_j / $took_r) -- it measured nothing"
elif [ "$t_j" -lt 10 ]; then
    fail "the journal run measured only ${t_j}ms -- too short to form a ratio, so this tier measured NOTHING"
else
    ratio10=$(( t_r * 10 / t_j ))
    if [ "$ratio10" -ge 50 ]; then
        pass "120 files into a 20,000-entry ledger: ${t_j}ms by journal, ${t_r}ms by snapshot rewrite -- $(( ratio10 / 10 )).$(( ratio10 % 10 ))x"
    else
        fail "the journal is only $(( ratio10 / 10 )).$(( ratio10 % 10 ))x cheaper than rewriting the snapshot (${t_j}ms vs ${t_r}ms) -- the reason for the journal has gone"
    fi
fi

printf 'TIER valgrind\n'
if vg_available; then
    rm -rf "$work/vg"; mkdir -p "$work/vg"
    if vg_run ./gbasic tests/intake_test.bas "$work/vg" >/dev/null 2>"$work/vg.err"; then
        pass "no definite leak or invalid access"
    else
        tail -30 "$work/vg.err"; fail "valgrind objected"
    fi
else
    pass "SKIP (valgrind unavailable)"
fi

exit $status
