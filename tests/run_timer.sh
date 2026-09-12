#!/usr/bin/env bash
# `timer` -- periodic work on the event loop (docs/timer_design.md).
#
# WHY IT EXISTS: every other source the loop polls is request-, reply- or
# transfer-driven, so nothing fired because time passed and periodic work had
# exactly one spelling -- `sleep` in a loop -- which on the event loop means the
# handler never returns and its worker never comes back. That is what gdash
# measured and misattributed to streams (GDASH-11).
#
# SELF-CHECKING RATHER THAN GOLDEN, and forced: every defect here is a
# PLAUSIBLE NUMBER OF TICKS. A timer firing twice as often, half as often, or
# one that silently caught up after a stall all produce output that reads
# exactly like a working timer, and a golden would record whichever count came
# out and defend it.
#
# TIME IS MEASURED FROM OUTSIDE, against the wall clock, and that is not a
# convenience: gBASIC's own clock is SECOND-RESOLUTION (`epoch(now())` cannot
# see a 0.25s sleep at all), so a fixture cannot time itself. It is also the
# stronger oracle -- the standard run_core.sh already holds `sleep` to.
set -u
cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh

make >/dev/null || { printf 'FAIL build\n'; exit 1; }

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
fails=0
ok()   { printf '  ok   %s\n' "$1"; }
bad()  { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }
now()  { date +%s.%N; }
since() { awk -v a="$1" -v b="$2" 'BEGIN{printf "%.3f", b-a}'; }
# awk rather than bash arithmetic: these are fractional seconds.
le()   { awk -v a="$1" -v b="$2" 'BEGIN{exit !(a<=b)}'; }
# valgrind under a BOUND. `vg_run` has none of its own, and an unbounded tier is
# one a hang turns green-forever rather than red -- which is how this one
# behaved the first time a perturbation stopped a one-shot retiring. It must
# still go THROUGH vg_run, because run_valgrind_policy's tripwire forbids a
# suite typing its own valgrind flags, so the bound is enforced from outside:
# its own session, so killing the leader takes valgrind with it.
#
# VG_FLAGS AND VG_EXTRA ARE PASSED IN THE ENVIRONMENT, not left to be inherited.
# `declare -f` carries the FUNCTION and not the variables it reads, so the first
# draft ran `valgrind ./gbasic ...` with the whole shared policy missing --
# including --error-exitcode, without which a real error reports success. A
# valgrind tier weaker than its neighbours is the exact drift run_valgrind_policy
# was built to end.
vg_timer() {
    VG_FLAGS="$VG_FLAGS" VG_EXTRA="${VG_EXTRA:-}" \
        setsid --wait sh -c "$(declare -f vg_run); vg_run $*" >/dev/null 2>&1 &
    local leader=$! i
    for i in $(seq 1 180); do
        kill -0 "$leader" 2>/dev/null || { wait "$leader"; return $?; }
        sleep 1
    done
    kill -9 -- -"$leader" 2>/dev/null || kill -9 "$leader" 2>/dev/null
    wait "$leader" 2>/dev/null
    printf '  (valgrind exceeded its 180s bound)\n'
    return 1
}

# --- SEMANTICS + REFUSALS --------------------------------------------------
printf 'TIER semantics\n'
out="$scratch/sem.out"
if timeout 60 ./gbasic tests/timer/timer_test.bas >"$out" 2>&1; then
    mism="$(sed -n 's/^mismatches: //p' "$out")"
    checks="$(sed -n 's/^checks: //p' "$out")"
    if [ "$mism" = "0" ] && [ "${checks:-0}" -ge 14 ]; then
        ok "$checks checks, 0 mismatches"
    else
        bad "timer_test: $checks checks, $mism mismatches"
        grep MISMATCH "$out" || true
    fi
else
    bad "timer_test exited nonzero"; cat "$out"
fi

# --- CADENCE ---------------------------------------------------------------
# Ten 0.1s ticks. A BAND, because it is a clock: the floor is what a timer
# firing too fast violates, the ceiling what one firing too slowly does.
printf 'TIER cadence\n'
t0="$(now)"
out="$(timeout 60 ./gbasic tests/timer/timer_cadence.bas 2>&1)"
t1="$(now)"
elapsed="$(since "$t0" "$t1")"
if [ "$out" = "DELIVERED 10 SKIPPED 0" ]; then
    ok "ten ticks delivered, none skipped"
else
    bad "cadence (said: $out)"
fi
if le 0.85 "$elapsed" && le "$elapsed" 3.0; then
    ok "ten 0.1s ticks took ${elapsed}s (band 0.85-3.0)"
else
    bad "cadence timing: ${elapsed}s outside the band 0.85-3.0"
fi

# --- COALESCE: the load-bearing tier ---------------------------------------
# A handler slower than the interval, and then a handler that is not. THE
# ASSERTION IS WHAT HAPPENS AFTER THE STALL ENDS, because that is the only
# place the two schedules differ observably: a fixture that merely counts ticks
# stops at its fixed count either way.
#
#   coalescing -- the next due time comes from the DELIVERY, so the timer is
#                 back on schedule at once. MEASURED: 4,0,0,0
#   catch-up   -- the backlog drains one per iteration, `skipped` counting DOWN
#                 as it unwinds. MEASURED: 12,11,10,9
#
# On the event loop that drain is a burst, and a burst is the unbounded-queue
# defect whose symptom is a HANG and not a failure.
printf 'TIER coalesce\n'
t0="$(now)"
out="$(timeout 90 ./gbasic tests/timer/timer_coalesce.bas 2>&1)"
t1="$(now)"
elapsed="$(since "$t0" "$t1")"
delivered="$(printf '%s\n' "$out" | sed -n 's/^DELIVERED \([0-9]*\) SKIPPED \([0-9]*\)$/\1/p')"
skipped="$(printf '%s\n' "$out" | sed -n 's/^DELIVERED \([0-9]*\) SKIPPED \([0-9]*\)$/\2/p')"
after="$(printf '%s\n' "$out" | sed -n 's/^AFTER //p')"
if [ -z "$delivered" ] || [ -z "$after" ]; then
    bad "coalesce (said: $out)"
else
    # Entry 1 reports the stall it followed and is timing-dependent; entries 2
    # onward must be ZERO, which is what "back on schedule at once" means.
    tail_vals="$(printf '%s' "$after" | cut -d, -f2-4 | tr -d ',')"
    n_after="$(printf '%s' "$after" | tr ',' '\n' | sed '/^$/d' | wc -l)"
    first="$(printf '%s' "$after" | cut -d, -f1)"
    if [ "$n_after" -lt 4 ]; then
        bad "coalesce: only $n_after post-stall ticks were reported ($after)"
    elif [ "$first" -le 0 ]; then
        # THE STALL ITSELF WENT UNREPORTED. A different defect from a backlog,
        # and it must say so: the first draft printed "a draining backlog is
        # catch-up" for this case, which is the reports-the-wrong-cause class
        # this tree has produced before.
        bad "coalesce: the tick after the stall reported skipped 0 ($after) -- lost intervals are not being counted"
    elif [ "$tail_vals" != "000" ]; then
        bad "coalesce: post-stall skipped was '$after' -- a backlog draining one per iteration is catch-up"
    else
        ok "the timer is back on schedule the tick after the stall (skipped: $after)"
    fi
    # AND THE SHORTFALL IS REPORTED, or coalescing is silent data loss. The
    # fixture's schedule is KNOWN: the first tick at 0.05s then three handlers
    # of 0.25s, so 0.80s of intervals must be accounted for by the stall, and
    # the four ticks after it add 0.20s more.
    accounted="$(awk -v d="$delivered" -v s="$skipped" 'BEGIN{printf "%.3f", (d+s)*0.05}')"
    if [ "$skipped" -gt 0 ] && le 0.85 "$accounted" && le "$accounted" 1.10 &&
       le "$accounted" "$elapsed"; then
        ok "every interval is delivered or reported skipped (${delivered}+${skipped} = ${accounted}s against a known ~0.95s schedule, inside ${elapsed}s of wall clock)"
    else
        bad "coalesce accounting: ${delivered}+${skipped} = ${accounted}s (schedule says ~0.95, wall ${elapsed}s)"
    fi
fi

# --- ONE-SHOT, and the control that it is not a program that cannot run -----
printf 'TIER one_shot\n'
t0="$(now)"
out="$(timeout 30 ./gbasic tests/timer/timer_oneshot.bas 2>&1)"; rc=$?
t1="$(now)"
elapsed="$(since "$t0" "$t1")"
if [ "$rc" = "0" ] && [ "$out" = "FIRED count=1 repeating=false" ] && le "$elapsed" 5.0; then
    ok "after() fires once and the program ends by itself (${elapsed}s, exit 0)"
else
    bad "one_shot (exit $rc, ${elapsed}s, said: $out)"
fi
# CONTROL: a repeating timer must NOT end on its own, or "it exited" would be
# true of a build where timers never worked at all.
# --line-buffered, because this process is KILLED: block-buffered lines sit in
# the pipe and never reach the file, so the control reported 0 ticks while the
# timer was working perfectly. PLAT-STREAM's own lesson, and run_http records it
# for the same reason.
timeout 3 ./gbasic --line-buffered tests/timer/timer_forever.bas >"$scratch/forever.out" 2>&1; rc=$?
ticks="$(grep -c '^TICK ' "$scratch/forever.out" || true)"
if [ "$rc" = "124" ] && [ "$ticks" -ge 10 ]; then
    ok "CONTROL: a repeating timer keeps the loop alive (killed at 3s after $ticks ticks)"
else
    bad "CONTROL forever: exit $rc after $ticks ticks (wanted a timeout kill)"
fi

# --- UNWATCHED: the 2108 warning, with its control -------------------------
printf 'TIER unwatched\n'
timeout 20 ./gbasic tests/timer/timer_unwatched.bas >"$scratch/uw.out" 2>"$scratch/uw.err"
if grep -q 'nothing watches `timer.ticks`' "$scratch/uw.err"; then
    ok "a timer nobody watches is reported"
else
    bad "unwatched: stderr said $(cat "$scratch/uw.err")"
fi
# THE POSITION IS THE CREATION SITE, not 0:0. The warning fires after `main`
# returns, where there is no current line; it is also the deduplication key.
if grep -q 'timer_unwatched.bas:5:5' "$scratch/uw.err"; then
    ok "and located at the timer's own creation site"
else
    bad "unwatched position: $(cat "$scratch/uw.err")"
fi
[ "$(cat "$scratch/uw.out")" = "ran" ] || bad "unwatched: the program must still run"
# CONTROL: a watched timer is silent, or the warning is noise.
timeout 30 ./gbasic tests/timer/timer_oneshot.bas 2>"$scratch/w.err" >/dev/null
if [ ! -s "$scratch/w.err" ]; then
    ok "CONTROL: a watched timer says nothing"
else
    bad "CONTROL watched: stderr said $(cat "$scratch/w.err")"
fi

# --- MONOTONIC: a source tripwire ------------------------------------------
# §3.2 cannot be tested from outside -- a test cannot move the system clock --
# so the claim is asserted by reading the line that makes it. An NTP step
# backwards stalls a wall-clock timer and a step forwards fires every interval
# it crossed, both silently.
printf 'TIER monotonic\n'
body="$(sed -n '/^static double timer_now_seconds/,/^}/p' src/eval.c)"
if printf '%s' "$body" | grep -q 'CLOCK_MONOTONIC' &&
   ! printf '%s' "$body" | grep -qE 'CLOCK_REALTIME|\btime\(|gettimeofday'; then
    ok "the timer clock is CLOCK_MONOTONIC and reads no wall clock"
else
    bad "monotonic: timer_now_seconds does not read CLOCK_MONOTONIC alone"
fi

# --- STREAM: the shape the ask asked for -----------------------------------
printf 'TIER stream\n'
if ! command -v curl >/dev/null 2>&1; then
    printf '  SKIP stream (curl not installed)\n'
else
    export GBASIC_PATH=stdlib
    log="$scratch/stream.log"
    timeout 60 ./gbasic --line-buffered tests/timer/timer_stream.bas >"$log" 2>&1 &
    srv=$!
    port=""
    for _ in $(seq 1 200); do
        port="$(sed -n 's/^PORT //p' "$log" | head -1)"
        [ -n "$port" ] && break
        sleep 0.05
    done
    if [ -z "$port" ]; then
        bad "stream (no port announced)"
    else
        timeout 3 curl -sN "http://127.0.0.1:$port/events" >"$scratch/ev.out" 2>&1 || true
        got="$(grep -c '^event: tick' "$scratch/ev.out" || true)"
        # A 3s read of a 0.2s timer: about 15. A band, and a floor that a
        # stream receiving only its opening event fails.
        if [ "$got" -ge 8 ] && [ "$got" -le 40 ]; then
            ok "a parked stream received $got timer-driven events it never asked for"
        else
            bad "stream: $got tick events in 3s of a 0.2s timer"
        fi
        grep -q '^data: open' "$scratch/ev.out" || bad "stream: the opening event never arrived"
        # AND THE WORKER IS FREE THROUGHOUT, which is the whole point: the
        # handler returned, so an ordinary request is still answered while the
        # stream is parked and ticking.
        timeout 3 curl -sN "http://127.0.0.1:$port/events" >"$scratch/ev2.out" 2>&1 &
        client=$!
        sleep 0.5
        pong="$(timeout 3 curl -s "http://127.0.0.1:$port/ping" || true)"
        if [ "$pong" = "pong" ]; then
            ok "and an ordinary request is answered while it ticks"
        else
            bad "stream: /ping said '$pong' with a stream parked"
        fi
        # WAIT ON THE CLIENT ONLY. A bare `wait` here reaps every background
        # job, and one of them is the server -- which does not exit until its
        # own timeout, turning the tier into a minute of nothing.
        kill "$client" 2>/dev/null || true
        wait "$client" 2>/dev/null || true
    fi
    kill "$srv" 2>/dev/null || true
    wait "$srv" 2>/dev/null || true
fi

# --- TERM: a timer must not outlive a drain -------------------------------
# FOUND BY THIS SUITE HANGING. A live timer kept the event loop alive after
# SIGTERM, so a program with a server and a timer NEVER EXITED -- and a worker
# told to drain has to exit, which is the whole of PLAT-WEB-2's rolling reload.
# MEASURED PRE-EXISTING: the same program with `watch(inbox.messages)` instead
# of a timer had the same defect and has since the inbox source was written.
# The timer only made the shape ordinary, since a dashboard with a refresh tick
# is exactly this program.
#
# THE RULE IS APPLIED AT TWO LEVELS and the perturbation had to disable BOTH to
# turn this tier red: `aux_loop_sources_active` decides whether the loop is
# entered and continued, and the per-iteration `aux_ok` decides whether the
# three sources are polled and serviced at all. They read the ONE flag, so they
# cannot disagree -- but a perturbation that removes only the first leaves the
# program exiting correctly, which is worth knowing before reading a green line
# here as proof that either half alone is load-bearing.
printf 'TIER term\n'
export GBASIC_PATH=stdlib
./gbasic --line-buffered tests/timer/timer_stream.bas >"$scratch/term.log" 2>&1 &
tp=$!
for _ in $(seq 1 100); do grep -q '^PORT ' "$scratch/term.log" && break; sleep 0.05; done
kill -TERM "$tp" 2>/dev/null || true
gone=""
for _ in $(seq 1 60); do
    kill -0 "$tp" 2>/dev/null || { gone=yes; break; }
    sleep 0.1
done
if [ -n "$gone" ]; then
    ok "a server with a live timer exits on SIGTERM"
else
    bad "term: a live timer kept the loop alive after SIGTERM"
    kill -9 "$tp" 2>/dev/null || true
fi
wait "$tp" 2>/dev/null || true
# CONTROL: the timer still keeps a NON-serving program alive, or the fix would
# be "a timer stops working", which every tier above would still pass.
timeout 3 ./gbasic --line-buffered tests/timer/timer_forever.bas >"$scratch/ctl.out" 2>&1; rc=$?
if [ "$rc" = "124" ]; then
    ok "CONTROL: with no server, a timer still holds the loop open"
else
    bad "CONTROL term: a plain timer program exited on its own (exit $rc)"
fi

# --- VALGRIND ---------------------------------------------------------------
printf 'TIER valgrind\n'
if vg_available; then
    # BOTH SHAPES, because they exercise different code: the one-shot retires
    # itself through `timer_compact`, and the repeating one grows the array,
    # delivers ten events carrying a descriptor apiece, and is cancelled.
    # BOUNDED, and -k because this interpreter installs a SIGTERM handler: a
    # bound that might not fire is not a bound. Without one, a defect that stops
    # a timer retiring hangs this tier forever, and A HANG IS NOT A FAILURE --
    # which is exactly how it behaved the first time it met one.
    if vg_timer ./gbasic tests/timer/timer_oneshot.bas; then
        ok "no definite leak or invalid access over a one-shot's whole life"
    else
        bad "valgrind (one-shot)"
    fi
    if vg_timer ./gbasic tests/timer/timer_cadence.bas; then
        ok "nor over ten deliveries and a cancel"
    else
        bad "valgrind (repeating)"
    fi
else
    printf '  SKIP valgrind (unavailable)\n'
fi

if [ "$fails" = "0" ]; then
    printf 'run_timer: all cases passed\n'
else
    printf 'run_timer: %d FAILED\n' "$fails"
    exit 1
fi
