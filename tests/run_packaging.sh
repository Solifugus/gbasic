#!/usr/bin/env bash
set -uo pipefail

# The shipping path: source -> lean interpreter -> .deb -> a running service.
#
# This exists because the packaging work was verified BY HAND and nothing
# re-ran it. A build script that has produced a working service exactly once,
# on the machine where it was written, is a hypothesis -- and packaging rots
# silently, because nobody rebuilds a .deb until the day they need to ship.
#
# The suite never needs root. `build-deb.sh` takes a RUNTIME override, which
# exists precisely so the whole path can be exercised somewhere writable: the
# package is built rooted in a temp directory, extracted, and RUN. That is the
# same code path a real package takes, with one variable different.
#
# Tiers:
#   BUILD      the package is produced, and its lean build really is lean
#   STRUCTURE  control metadata, conffiles, maintainer scripts, permissions
#   SUBSTITUTE no @RUNTIME@ token survives into the shipped sources
#   RUN        the extracted service starts, answers /health, stores and
#              returns data -- the only tier that proves the package works
#   QUIET      startup writes no warning; unused-result on `serve` would land
#              in an operator's journal on every start
#   SHADOW     the library-search hazard: a stray NAME.bas beneath an app
#              REPLACES the shipped library, and loading by absolute path is
#              what stops it. Both halves asserted, because a defence nobody
#              proves is a comment.
#
# Skips cleanly without dpkg-deb or curl.

cd "$(dirname "$0")/.."
root="$(pwd)"

if ! command -v dpkg-deb >/dev/null 2>&1; then
    printf 'SKIP run_packaging (dpkg-deb not installed)\n'
    exit 0
fi
if ! command -v curl >/dev/null 2>&1; then
    printf 'SKIP run_packaging (curl not installed)\n'
    exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"; [ -n "${svc_pid:-}" ] && kill "$svc_pid" 2>/dev/null' EXIT
svc_pid=""

failures=0
checks=0
pass() { checks=$((checks + 1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); printf '  FAIL %s\n' "$1"; }

RT="$work/rt"
DEB="$root/packaging/notesd_0.1.0_$(dpkg --print-architecture).deb"

printf 'TIER build\n'
if RUNTIME="$RT" ./packaging/build-deb.sh packaging/example-app >"$work/build.log" 2>&1; then
    pass 'build-deb.sh produced a package'
else
    fail 'build-deb.sh produced a package'
    tail -20 "$work/build.log"
    printf 'FAIL tests/run_packaging.sh (%d of %d)\n' "$failures" "$checks"
    exit 1
fi

# The lean build is the reason a shipped app does not drag a desktop toolkit
# onto a server. Asserted as a CEILING, not an exact count: the number moves
# with the distribution's own transitive deps, but "fewer than twenty" is the
# claim, and the full build is ~48.
libs=$(command grep -oE 'links [0-9]+ shared' "$work/build.log" | command grep -oE '[0-9]+' || echo 999)
if [ "$libs" -lt 20 ]; then
    pass "the lean interpreter links $libs shared libraries (full build is ~48)"
else
    fail "the lean interpreter links $libs shared libraries, expected under 20"
fi

printf 'TIER structure\n'
ctl="$(dpkg-deb -I "$DEB" 2>/dev/null)"
for field in "Package: notesd" "Architecture:" "Depends:" "Maintainer:"; do
    if printf '%s' "$ctl" | command grep -q "$field"; then
        pass "control has $field"
    else
        fail "control has $field"
    fi
done
if dpkg-deb -I "$DEB" conffiles 2>/dev/null | command grep -q '/etc/notesd/notesd.conf'; then
    pass 'the config file is a conffile (dpkg will not clobber operator edits)'
else
    fail 'the config file is a conffile'
fi
contents="$(dpkg-deb -c "$DEB")"
# The package root must be traversable. mktemp -d gives 0700, and a package
# built from it installs a directory nobody but root can enter.
if printf '%s' "$contents" | command grep -qE '^drwxr-xr-x .* \./$'; then
    pass 'the package root is world-traversable'
else
    fail 'the package root is world-traversable'
    printf '%s\n' "$contents" | head -2
fi
for want in "/lib/systemd/system/notesd.service" "/app/notesd.bas" "/stdlib/stats.bas"; do
    if printf '%s' "$contents" | command grep -q -- "$want"; then
        pass "package contains $want"
    else
        fail "package contains $want"
    fi
done

printf 'TIER substitution\n'
dpkg-deb -x "$DEB" "$work/x"
appsrc="$work/x$RT/app/notesd.bas"
if [ -f "$appsrc" ]; then
    if command grep -q '@RUNTIME@' "$appsrc"; then
        fail 'no @RUNTIME@ token survives into the shipped sources'
    else
        pass 'no @RUNTIME@ token survives into the shipped sources'
    fi
else
    fail "the application source is at the expected path ($appsrc)"
fi

printf 'TIER run\n'
mkdir -p "$RT" "$work/state"
cp -r "$work/x$RT/." "$RT/" 2>/dev/null

# PORT 0: the kernel assigns. A fixed port made this suite unrepeatable and,
# worse, made it talk to a STRANGER -- a previous run's process still holding
# the port answered /health while its database had already been deleted, so the
# POST failed with a 500 that had nothing to do with the package. The port is
# read back from the service's own startup line, which also exercises the
# `port: 0` path an operator uses behind a reverse proxy.
printf 'port = 0\ndatabase = %s/state/notes.db\n' "$work" > "$work/notesd.conf"
# `exec`, and the `&` on the SUBSHELL rather than inside it. Without both, `$!`
# is the PID of the wrapper subshell and not of gbasic, so the teardown below
# kills the wrapper and leaves the server running with its parent gone. That is
# not hypothetical: it leaked one orphan PER RUN, and 64 were found alive on
# this machine, the oldest after two days (2026-08-30). `exec` makes the
# subshell BECOME gbasic, so the recorded pid is the one that must die.
( cd / && export NOTESD_CONF="$work/notesd.conf" \
  && exec "$RT/gbasic" --line-buffered "$RT/app/notesd.bas" ) \
    >"$work/svc.log" 2>&1 &
svc_pid=$!
sleep 1

port=""
for _ in 1 2 3 4 5 6 7 8 9 10; do
    port="$(command grep -oE 'listening on 127\.0\.0\.1:[0-9]+' "$work/svc.log" 2>/dev/null \
            | command grep -oE '[0-9]+$' | head -1)"
    [ -n "$port" ] && break
    sleep 0.5
done
if [ -z "$port" ]; then
    fail 'the packaged service reports the port it bound'
    cat "$work/svc.log"
    printf 'FAIL tests/run_packaging.sh (%d of %d checks failed)\n' "$failures" "$checks"
    exit 1
fi
pass "the service reports its OS-assigned port ($port)"

ready=0
for _ in 1 2 3 4 5 6 7 8 9 10; do
    if [ "$(curl -s --max-time 2 "http://127.0.0.1:$port/health" 2>/dev/null)" = "ok" ]; then
        ready=1
        break
    fi
    sleep 0.5
done
if [ "$ready" = "1" ]; then
    pass 'the packaged service starts and answers /health'
else
    fail 'the packaged service starts and answers /health'
    cat "$work/svc.log"
fi

code="$(curl -s -o /dev/null -w '%{http_code}' --max-time 3 \
        -d 'body=packaged+and+running' "http://127.0.0.1:$port/notes" 2>/dev/null)"
if [ "$code" = "303" ]; then
    pass 'a POST is accepted and redirects'
else
    fail "a POST is accepted and redirects (got HTTP $code)"
    # A handler failure is reported on the service's own stderr; a test that
    # says only "500" makes the reader reproduce it by hand.
    printf '    --- service log ---\n'; sed 's/^/    /' "$work/svc.log"
fi
if curl -s --max-time 3 "http://127.0.0.1:$port/" 2>/dev/null | command grep -q 'packaged and running'; then
    pass 'the stored value comes back -- sqlite works from the packaged runtime'
else
    fail 'the stored value comes back'
fi

printf 'TIER quiet startup\n'
# Two informational lines on stderr and nothing else. A warning here would
# reach an operator's journal on every single service start.
if command grep -qE 'warning:' "$work/svc.log"; then
    fail 'startup emits no warning'
    command grep -E 'warning:' "$work/svc.log" | head -3
else
    pass 'startup emits no warning'
fi

# Wait for it to actually go. `kill` returns immediately, and a lingering
# process is what created the stranger problem above.
kill "$svc_pid" 2>/dev/null
for _ in 1 2 3 4 5 6 7 8 9 10; do
    kill -0 "$svc_pid" 2>/dev/null || break
    sleep 0.2
done
svc_pid=""

printf 'TIER library-shadowing defence\n'
# The hazard, demonstrated: a bare `load` searches the app's own directory tree
# recursively and FIRST, so a stray file wins. Then the defence: the same
# program loading by absolute path is unaffected by the same stray file.
mkdir -p "$work/shadow/app/vendor/deep"
cat > "$work/shadow/app/vendor/deep/stats.bas" <<'EOF'
library stats
    function sharpe_ratio(r, rf, periods)
        return 999
    end function
end library
EOF
cat > "$work/shadow/app/bare.bas" <<'EOF'
load stats
print string(stats.sharpe_ratio([0.01, 0.02, -0.01], 0, 252))
EOF
cat > "$work/shadow/app/pinned.bas" <<EOF
load stats from "$RT/stdlib/stats.bas"
print string(stats.sharpe_ratio([0.01, 0.02, -0.01], 0, 252))
EOF
bare="$(cd / && "$RT/gbasic" "$work/shadow/app/bare.bas" 2>/dev/null)"
pinned="$(cd / && "$RT/gbasic" "$work/shadow/app/pinned.bas" 2>/dev/null)"
if [ "$bare" = "999" ]; then
    pass 'the hazard is real: a stray stats.bas under the app wins over the shipped one'
else
    fail "the hazard is real (bare load returned '$bare', expected the stray's 999)"
fi
if [ "$pinned" != "999" ] && [ -n "$pinned" ]; then
    pass "loading by absolute path is immune (got $pinned, not the stray's 999)"
else
    fail "loading by absolute path is immune (got '$pinned')"
fi

rm -f "$DEB"

if [ "$failures" -gt 0 ]; then
    printf 'FAIL tests/run_packaging.sh (%d of %d checks failed)\n' "$failures" "$checks"
    exit 1
fi
printf 'PASS tests/run_packaging.sh (%d checks)\n' "$checks"
