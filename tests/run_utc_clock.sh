#!/usr/bin/env bash
# Every instant gBASIC makes is UTC (2026-11-10, Matthew: "The default is
# UTC"): now(), from_epoch(), file_mtime and a directory entry's `modified`,
# and epoch(dt)/number(dt)/set_mtime read a datetime as UTC. The zone is
# chosen at the edges -- now(zone), to_zone, from_zone, zone_offset(dt), with
# "local" naming the system's own -- and pg sends a datetime parameter with
# "+00", so a timestamptz column stores the same instant.
#
# Before, all of these were LOCAL while a datetime read from a text with an
# offset was UTC, and a program holding both was wrong by the machine's
# offset. The ordinary gate runs in one zone, which is how that went unseen,
# so this suite runs the same program under five.
#
# Tiers:
#   1. ZONES -- tests/utc_clock_test.bas under TZ = UTC, America/New_York,
#      Asia/Kolkata, Pacific/Chatham (+13:45) and America/St_Johns (-03:30).
#      The system offset it is held to is Python's zoneinfo's, not gBASIC's.
#   2. STABLE -- the "-- stable" block prints the same bytes in every zone,
#      and its UTC values are Python's (datetime.fromtimestamp(tz=utc)).
#   3. STAT -- the file the program stamped has the mtime `stat` reports.
#   4. TRIPWIRE -- the only local-time calls in src/eval.c are in the zone
#      helpers (zone_local_offset_at, zone_localtime, zone_resolve_instants).
#      A new one is either an edge that names "local" or the old defect back.
#   5. VALGRIND, over tier 1 in one zone.
#
# Tiers 1-2 hold the offset to Python and skip without python3.
. "$(dirname "$0")/portable.sh"   # GNU coreutils behaviour where the tools are BSD
set -u
cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
export GBASIC_PATH=stdlib

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_utc_clock: build failed\n'
    exit 1
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0
fail() { printf 'FAIL %s\n' "$1"; status=1; }

zones="UTC America/New_York Asia/Kolkata Pacific/Chatham America/St_Johns"
if command -v python3 >/dev/null 2>&1 && python3 -I -c 'import zoneinfo; zoneinfo.ZoneInfo("Pacific/Chatham")' 2>/dev/null; then
    printf -- '-- zones\n'
    for tz in $zones; do
        off=$(python3 -I -c 'import sys, datetime, zoneinfo
t = datetime.datetime(2027, 4, 6, 12, 52, tzinfo=datetime.timezone.utc)
print(int(t.astimezone(zoneinfo.ZoneInfo(sys.argv[1])).utcoffset().total_seconds() // 60))' "$tz")
        d="$work/$(printf '%s' "$tz" | tr '/' '_')"
        mkdir -p "$d"
        out=$(TZ=$tz timeout 60 ./gbasic tests/utc_clock_test.bas "$d" "$off" 2>"$work/err")
        printf '%s\n' "$out" >"$d.out"
        n=$(printf '%s\n' "$out" | sed -n 's/^checks: \([0-9]*\)$/\1/p')
        if [ "$(printf '%s\n' "$out" | tail -1)" = "mismatches: 0" ] && [ "${n:-0}" -ge 11 ] && [ ! -s "$work/err" ]; then
            printf 'PASS zone %s (offset %s min, %s checks)\n' "$tz" "$off" "$n"
        else
            printf '%s\n' "$out" | grep -v '^ok ' | head -20
            head -5 "$work/err"
            fail "zone $tz (floor 11 checks, stderr empty)"
        fi
        # stat, from outside: the stamped file's mtime is the epoch printed
        want=$(printf '%s\n' "$out" | sed -n 's/^stamped //p')
        got=$(stat -c %Y "$d/stamped.txt" 2>/dev/null)
        [ -n "$want" ] && [ "$want" = "$got" ] || fail "zone $tz: stat reads mtime $got, the program stamped $want"
        sed -n '/^-- stable$/,/^$/p' "$d.out" >"$d.stable"
    done

    printf -- '-- stable across zones, and held to Python\n'
    first=""
    same=1
    for tz in $zones; do
        s="$work/$(printf '%s' "$tz" | tr '/' '_').stable"
        if [ -z "$first" ]; then
            first=$s
        elif ! cmp -s "$first" "$s"; then
            diff "$first" "$s" | head -6
            fail "the stable block differs under TZ=$tz"
            same=0
        fi
    done
    python3 -I - "$first" >"$work/py.txt" <<'EOF'
import sys, datetime
u = datetime.timezone.utc
f = lambda n: datetime.datetime.fromtimestamp(n, u).strftime("%Y-%m-%d %H:%M:%S")
e = lambda *a: int(datetime.datetime(*a, tzinfo=u).timestamp())
print("-- stable")
print(f(0)); print(f(1700000000)); print(f(1772951400))
print(e(2000, 1, 1)); print(e(2026, 3, 8, 7, 30)); print(e(2026, 11, 1, 6, 30))
print(1793511000)
print(f(e(2027, 4, 6, 12, 52)))
print("2027-04-06 21:52:00")
EOF
    # the first nine lines are Python's; the rest are checked by tier 1
    if head -10 "$first" | cmp -s - "$work/py.txt"; then
        [ "$same" = 1 ] && printf 'PASS stable (identical under %s zones; the instants are Python'"'"'s)\n' "$(echo $zones | wc -w)"
    else
        diff <(head -10 "$first") "$work/py.txt"
        fail "the stable block's instants are not Python's"
    fi
else
    printf 'SKIP zones and stable (no python3 zoneinfo with Pacific/Chatham: no outside offset to hold to)\n'
fi

printf -- '-- tripwire: local time only in the zone helpers\n'
calls=$(awk '
    /^(static |int |long |double |char |Value |void )[^;]*\(/ { fn = $0; sub(/\(.*/, "", fn); n = split(fn, w, /[ *]+/); fn = w[n] }
    /gb_localtime\(|gb_mktime\(|[^_a-z]mktime\(|localtime_r\(/ && !/^ *\*/ && !/^ *\/\*/ { print fn }
' src/eval.c | sort | uniq -c | sed 's/^ *//')
want_calls=$'1 zone_local_offset_at\n1 zone_localtime\n4 zone_resolve_instants'
if [ "$calls" = "$want_calls" ]; then
    printf 'PASS tripwire (local time is read only by the zone helpers)\n'
else
    printf '%s\n' "$calls"
    fail "tripwire: a local-time call outside the zone helpers in src/eval.c; every instant is UTC, and local time is an edge (now(\"local\"), to_zone)"
fi

if vg_available; then
    if TZ=America/New_York timeout 600 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic tests/utc_clock_test.bas "$1" -240' _ "$work" >/dev/null 2>"$work/vg"; then
        printf 'PASS valgrind tests/utc_clock_test.bas\n'
    else
        printf 'FAIL valgrind\n'; head -20 "$work/vg"; status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit $status
