#!/usr/bin/env bash
# THE DIFFERENCE OF TWO CIVIL DATETIMES, RUN IN SEVERAL TIMEZONES.
#
# WHY A SUITE OF ITS OWN: the assertion cannot live inside one process. The zone
# is chosen before gBASIC starts, and a golden captured in one zone agrees with
# itself -- which is exactly how this shipped. The local gate runs in whatever
# zone the developer's box is set to; CI runs UTC; nobody compared them.
#
# WHAT IT PINS: a gBASIC `datetime` is CIVIL and carries no zone, which is why
# `epoch(dt, zone)` exists and takes one. So `b - a` is calendar arithmetic and
# the machine's zone has no business in it. It was in it -- the subtraction used
# `mktime` with `tm_isdst = -1` -- so an interval spanning a spring-forward
# transition came out AN HOUR SHORT:
#
#   dates.between({date}"2026-02-01", {date}"2026-04-02", "days")
#     America/New_York 59.958333        UTC 60
#
# AND IT WAS A WRONG ANSWER IN A FINANCE LIBRARY. `stdlib/credit.bas`'s
# delinquency ladder tests `days >= 60`, so in a US timezone a loan SIXTY DAYS
# PAST DUE was bucketed `dpd_30` -- one step too healthy, silently, and
# differently in different offices. `examples/credit_cookbook/02_delinquency.out`
# had the New York answer committed as expected, which is what a golden does to a
# defect it was captured under.
#
# THE ZONES ARE CHOSEN, NOT COLLECTED. Each one asks a different question:
#   UTC                no transitions at all -- the control
#   America/New_York   spring-forward in the middle of the measured interval
#   Europe/London      a DIFFERENT transition date, so a fix tuned to US rules fails
#   Asia/Kolkata       +05:30, a NON-WHOLE-HOUR offset, which rounds differently
#   Pacific/Kiritimati +14:00, the largest offset there is
#   Australia/Lord_Howe  +10:30/+11:00, a THIRTY-MINUTE DST step rather than an hour
. "$(dirname "$0")/portable.sh"
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null
status=0

ZONES="UTC America/New_York Europe/London Asia/Kolkata Pacific/Kiritimati Australia/Lord_Howe"

echo "--- TIER 1: the same answers in every zone ---"
first=""
for z in $ZONES; do
    out="$({ TZ="$z" GBASIC_PATH=stdlib timeout -k 5 60 ./gbasic tests/dates/civil_difference_test.bas 2>&1 </dev/null || true; })"
    bad="$(printf '%s\n' "$out" | sed -n 's/^mismatches: //p')"
    checks="$(printf '%s\n' "$out" | sed -n 's/^checks: //p')"
    if [ "${bad:-1}" != "0" ] || [ -z "$checks" ]; then
        printf 'FAIL %s\n' "$z"
        printf '%s\n' "$out" | grep -E '^MISMATCH|error' | head -4 | sed 's/^/  /'
        status=1
        continue
    fi
    # A COVERAGE FLOOR per zone: a run that stopped asserting also reports zero
    # mismatches, and a tzdata without the zone would silently fall back to UTC.
    if [ "$checks" -lt 11 ]; then
        printf 'FAIL %s (only %s checks ran)\n' "$z" "$checks"; status=1; continue
    fi
    printf 'ok   %-20s %s checks\n' "$z" "$checks"
    # AND THE ANSWERS MUST BE IDENTICAL ACROSS ZONES, not merely each-zone-correct:
    # that is the property, and per-zone passes alone would be satisfied by a
    # fixture whose expectations were somehow zone-aware.
    body="$(printf '%s\n' "$out" | grep '^ok  ' | sed 's/ *$//')"
    if [ -z "$first" ]; then
        first="$body"
    elif [ "$body" != "$first" ]; then
        printf 'FAIL %s produced different output from the first zone\n' "$z"
        diff <(printf '%s\n' "$first") <(printf '%s\n' "$body") | head -6 | sed 's/^/  /'
        status=1
    fi
done

echo "--- TIER 2: the credit recipe, which is where it bit ---"
# THE END-TO-END CASE, through the real library and its real golden. Tier 1 asserts
# the primitive; this asserts that the thing the primitive broke is fixed, and it
# is the check that would have caught the original defect with no knowledge of
# timezones at all.
cred_first=""
for z in UTC America/New_York Asia/Kolkata; do
    out="$({ TZ="$z" GBASIC_PATH=stdlib timeout -k 5 120 ./gbasic examples/credit_cookbook/02_delinquency.bas 2>&1 </dev/null || true; })"
    if [ -z "$cred_first" ]; then
        cred_first="$out"
        if ! diff -q <(printf '%s\n' "$out") examples/credit_cookbook/02_delinquency.out >/dev/null 2>&1; then
            printf 'FAIL the recipe does not match its golden in %s\n' "$z"
            diff examples/credit_cookbook/02_delinquency.out <(printf '%s\n' "$out") | head -8 | sed 's/^/  /'
            status=1
        else
            printf 'ok   %-20s matches the committed golden\n' "$z"
        fi
    elif [ "$out" != "$cred_first" ]; then
        printf 'FAIL the recipe answers differently in %s\n' "$z"
        diff <(printf '%s\n' "$cred_first") <(printf '%s\n' "$out") | head -8 | sed 's/^/  /'
        status=1
    else
        printf 'ok   %-20s same answer\n' "$z"
    fi
done

echo "--- TIER 3: tzdata is really installed, or this suite measures nothing ---"
# WITHOUT THIS THE WHOLE SUITE IS VACUOUS. An unknown TZ silently falls back to
# UTC, so on a machine with no tzdata every zone above would agree -- perfectly,
# and about nothing. Asserted by requiring a zone to CHANGE something the
# interpreter reports: `zone_offset` of a fixed instant.
# A REAL FILE, not /dev/stdin -- the trap run_bound.sh records in as many words:
# written that way the probe reported nothing at all and this tier printed no
# lines and exited 0, which reads exactly like a pass in scroll-back. Measured
# while writing it.
probe_src="$(mktemp)"
trap 'rm -f "$probe_src"' EXIT
cat >"$probe_src" <<'BAS'
d {datetime}= "2026-07-01 12:00:00"
print string(zone_offset(d))
BAS
probe() { TZ="$1" GBASIC_PATH=stdlib ./gbasic "$probe_src" 2>&1; }
u="$(probe UTC)"
k="$(probe Asia/Kolkata)"
n="$(probe America/New_York)"
if [ "$u" != "$k" ] && [ "$u" != "$n" ] && [ "$k" != "$n" ]; then
    printf 'ok   tzdata present: UTC=%s Kolkata=%s New_York=%s all differ\n' "$u" "$k" "$n"
else
    printf 'FAIL the zones do not differ (UTC=%s Kolkata=%s New_York=%s)\n' "$u" "$k" "$n"
    printf '     tzdata is probably missing, and TIER 1 would agree about nothing\n'
    status=1
fi

exit $status
