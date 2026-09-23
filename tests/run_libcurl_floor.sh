#!/usr/bin/env bash
set -uo pipefail

# gBASIC MUST COMPILE AGAINST THE libcurl OF THE PLATFORM IT SHIPS FOR.
#
# WHAT HAPPENED. Three call sites used CURLOPT_PROTOCOLS_STR and
# CURLOPT_REDIR_PROTOCOLS_STR, which arrived in libcurl 7.85.0 (2022-08-31).
# ubuntu:22.04 ships 7.81.0 -- and 22.04 is not an arbitrary old distribution,
# it is THE BASE IMAGE tools/build-release-tarball.sh builds in, chosen for its
# glibc floor. So on the exact platform the download targets, `make` FAILED
# with five errors. Nothing here could see it: this machine has 8.18.0, and a
# development box is always the newest thing in the room.
#
# THE SHAPE OF THE DEFECT IS WORSE THAN THE INSTANCE. An optional dependency
# that is MISSING degrades to a clean runtime error -- that is the whole
# HAVE_* convention. An optional dependency that is PRESENT BUT OLDER took the
# entire binary down, including every feature that has nothing to do with it.
# The tarball only escaped because it builds lean and never links libcurl at
# all, which means the one configuration nobody could ship was also the one
# nobody was testing.
#
# TIERS
#   FLOOR    every libcurl identifier our sources use must EXIST at the floor,
#            unless it sits inside a `#if LIBCURL_VERSION_NUM >=` guard.
#            Hermetic, instant, and general -- it catches the NEXT symbol, not
#            just the five that were found.
#   CONTROL  the scanner must FLAG a planted post-floor use. Without it "no
#            offenders" is equally satisfied by a scanner that reads nothing,
#            which is how a tripwire becomes a comment.
#   GUARD    the protocol restriction must survive in BOTH branches of the
#            shim. The cheapest way to make a floor failure go away is to
#            delete the option, and that trades a red build for a silently
#            missing security setting.
#   BUILD    the real thing: the FULL configuration compiled in the floor
#            image. Needs podman and a network, so it is opt-in -- and it says
#            NOT RUN rather than printing nothing, because a tier that is
#            silent about being absent reads exactly like one that passed.
#
# Headless, no network, never skips the first three.

cd "$(dirname "$0")/.."
root="$(pwd)"

SYMS=tests/libcurl_floor/symbols.txt
SOURCES="src/eval.c src/modules/smtp.c src/modules/xml.c src/modules/ldap.c src/modules/rowmodel.c src/modules/xlsx.c"

failures=0
checks=0
pass() { checks=$((checks + 1)); printf '  ok   %s\n' "$1"; }
fail() { checks=$((checks + 1)); failures=$((failures + 1)); printf '  FAIL %s\n' "$1"; }

if [ ! -f "$SYMS" ]; then
    printf 'FAIL run_libcurl_floor: %s is missing (tools/make_libcurl_floor_symbols.sh regenerates it)\n' "$SYMS"
    exit 1
fi

# Report every libcurl identifier used OUTSIDE a version guard that the floor
# does not have. The guard tracking is deliberately simple and deliberately
# CONSERVATIVE: a `#if LIBCURL_VERSION_NUM` region (and everything nested in
# it) is exempt, anything else is not. Erring towards exempting too little
# costs a false alarm somebody reads; erring the other way costs a build
# nobody can make.
scan() {
    awk -v symfile="$SYMS" '
    # COMMENTS ARE PROSE AND THIS FILE IS FULL OF IT. smtp.c argues about
    # dot-stuffing in a block comment that says "LIBCURL'"'"'S", and a scanner
    # that reads comments reports the ARGUMENT for a rule as a breach of it.
    # `//` is stripped only when it is not preceded by `:`, so the `//` in
    # "http://host" stays part of the string it belongs to.
    function strip(line,   out, i, j) {
        out = ""
        while (length(line) > 0) {
            if (in_comment) {
                i = index(line, "*/")
                if (i == 0) return out
                line = substr(line, i + 2)
                in_comment = 0
            } else {
                i = index(line, "/*")
                j = 0
                for (k = 1; k < length(line); k++) {
                    if (substr(line, k, 2) == "//" && substr(line, k - 1, 1) != ":") { j = k; break }
                }
                if (j > 0 && (i == 0 || j < i)) return out substr(line, 1, j - 1)
                if (i == 0) return out line
                out = out substr(line, 1, i - 1)
                line = substr(line, i + 2)
                in_comment = 1
            }
        }
        return out
    }
    BEGIN {
        while ((getline line < symfile) > 0) {
            if (line ~ /^#/ || line == "") continue
            known[line] = 1
        }
    }
    { $0 = strip($0) }
    /^[[:space:]]*#[[:space:]]*if/ {
        if ($0 ~ /LIBCURL_VERSION_NUM/) guarded = 1
        if (guarded) depth++
        next
    }
    /^[[:space:]]*#[[:space:]]*endif/ {
        if (guarded) { depth--; if (depth <= 0) { guarded = 0; depth = 0 } }
        next
    }
    guarded { next }
    {
        line = $0
        while (match(line, /(LIBCURL|CURL)[A-Za-z0-9_]*/)) {
            name = substr(line, RSTART, RLENGTH)
            # A WORD BOUNDARY IS REQUIRED AND awk HAS NO \b. Without this the
            # scanner reads `GB_CURL_PROTOCOLS` as `CURL_PROTOCOLS` and
            # `HAVE_LIBCURL` as `LIBCURL`, and reports the very shim that
            # fixes the defect as an instance of it.
            boundary = (RSTART == 1) || (substr(line, RSTART - 1, 1) !~ /[A-Za-z0-9_]/)
            line = substr(line, RSTART + RLENGTH)
            if (!boundary) continue
            if (name in known) continue
            if (name in seen) continue
            seen[name] = 1
            printf "%s:%d: %s\n", FILENAME, FNR, name
        }
    }
    ' "$@"
}

printf 'TIER floor\n'
offenders="$(scan $SOURCES)"
if [ -z "$offenders" ]; then
    pass 'every libcurl name used outside a version guard exists at the floor'
else
    fail 'a libcurl name newer than the build floor is used unguarded'
    printf '%s\n' "$offenders" | sed 's/^/       /'
    printf '       floor: %s\n' "$(sed -n 's/^# libcurl \(.*\)$/\1/p' "$SYMS" | head -1)"
fi

printf 'TIER control\n'
# The pre-fix source, in miniature. If the scanner cannot see this, the tier
# above is a green line that read nothing.
plant="$(mktemp --suffix=.c)"
cat > "$plant" <<'PLANT'
#include <curl/curl.h>
void f(CURL *h) {
    curl_easy_setopt(h, CURLOPT_PROTOCOLS_STR, "http,https");
}
PLANT
if scan "$plant" | grep -q 'CURLOPT_PROTOCOLS_STR'; then
    pass 'the scanner flags a post-floor name that is not guarded'
else
    fail 'the scanner flags a post-floor name that is not guarded'
fi
# ... and must NOT flag the same name once it is properly guarded, or the rule
# would forbid the very shim that fixes it.
guarded_plant="$(mktemp --suffix=.c)"
cat > "$guarded_plant" <<'PLANT'
#include <curl/curl.h>
#if LIBCURL_VERSION_NUM >= 0x075500
#define SET(h) curl_easy_setopt((h), CURLOPT_PROTOCOLS_STR, "http,https")
#else
#define SET(h) curl_easy_setopt((h), CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTP)
#endif
void f(CURL *h) { SET(h); }
PLANT
if scan "$guarded_plant" | grep -q 'CURLOPT_PROTOCOLS_STR'; then
    fail 'a guarded post-floor name is left alone'
else
    pass 'a guarded post-floor name is left alone'
fi
rm -f "$plant" "$guarded_plant"

printf 'TIER guard\n'
# BOTH branches must still restrict the protocol set. Deleting the option is
# the cheapest way to make a floor failure disappear, and it turns a build
# error into a missing restriction nothing else in this tree checks.
if grep -q 'CURLOPT_PROTOCOLS_STR' src/eval.c && grep -q 'CURLOPT_PROTOCOLS,' src/eval.c; then
    pass 'the shim restricts protocols on both sides of the version test'
else
    fail 'the shim restricts protocols on both sides of the version test'
fi
# AND THE OPTION MUST STILL BE SET SOMEWHERE. The shim can be present and
# correct while every call to it has been deleted, which is the same missing
# restriction by a quieter route -- so the uses are COUNTED. Three requests are
# configured in this tree (webclient's easy handle, http's multi handle, and
# smtp) and two of them can be redirected.
uses="$(grep -c 'GB_CURL_PROTOCOLS(' $SOURCES | awk -F: '{n += $2} END {print n+0}')"
redir="$(grep -c 'GB_CURL_REDIR_PROTOCOLS(' $SOURCES | awk -F: '{n += $2} END {print n+0}')"
# the two #define lines for each macro live in src/eval.c and are not call sites
uses=$((uses - 2)); redir=$((redir - 2))
if [ "$uses" -ge 3 ] && [ "$redir" -ge 2 ]; then
    pass "every request still restricts its protocols ($uses set, $redir also on redirect)"
else
    fail "every request still restricts its protocols (found $uses set, $redir on redirect; want >=3 and >=2)"
fi
raw="$(grep -n 'curl_easy_setopt([^,]*, *CURLOPT_\(REDIR_\)\?PROTOCOLS' $SOURCES | grep -v '^src/eval.c:[0-9]*: *curl_easy_setopt((h)' || true)"
if [ -z "$raw" ]; then
    pass 'no call site sets the protocol option without going through the shim'
else
    fail 'a call site sets the protocol option directly, bypassing the shim'
    printf '%s\n' "$raw" | sed 's/^/       /'
fi

printf 'TIER build\n'
if [ "${LIBCURL_FLOOR_BUILD:-0}" != "1" ]; then
    printf '  NOT RUN  the floor build was not attempted, so NOTHING here was\n'
    printf '           compiled against libcurl %s. The tiers above are a\n' \
        "$(sed -n 's/^# libcurl \([0-9.]*\).*/\1/p' "$SYMS" | head -1)"
    printf '           symbol comparison, not a build.\n'
    printf '           LIBCURL_FLOOR_BUILD=1 %s\n' "$0"
else
    runtime=""
    for c in podman docker; do command -v "$c" >/dev/null 2>&1 && { runtime="$c"; break; }; done
    if [ -z "$runtime" ]; then
        fail 'LIBCURL_FLOOR_BUILD=1 was asked for but neither podman nor docker is installed'
    else
        log="$(mktemp)"
        "$runtime" run --rm -v "$root":/src:ro "${IMAGE:-docker.io/library/ubuntu:22.04}" bash -c '
set -u
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq build-essential bison pkg-config \
  libsqlite3-dev zlib1g-dev libxml2-dev libssl-dev libcurl4-openssl-dev >/dev/null 2>&1
mkdir -p /work && cp -r /src /work/gbasic && cd /work/gbasic
make clean >/dev/null 2>&1
make >/tmp/b.log 2>&1
rc=$?
echo "make exit=$rc"
grep "error:" /tmp/b.log | head -20
exit $rc
' >"$log" 2>&1
        rc=$?
        if [ "$rc" -eq 0 ]; then
            pass 'the full configuration compiles against the floor libcurl'
        else
            fail 'the full configuration compiles against the floor libcurl'
            sed 's/^/       /' "$log" | head -25
        fi
        rm -f "$log"
    fi
fi

printf '\nrun_libcurl_floor: %d checks, %d failed\n' "$checks" "$failures"
[ "$failures" -eq 0 ]
