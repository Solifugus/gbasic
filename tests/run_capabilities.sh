#!/usr/bin/env bash
# THE CAPABILITY SURFACE, AND THE TRIPWIRE THAT KEEPS IT COMPLETE
# (docs/library_trust_design.md §1.3).
#
# `gbasic --capabilities` reports what this interpreter can touch, so a scanner
# in another project DERIVES the surface instead of hardcoding it. The whole
# scheme rests on a capability scan being a PROOF rather than a guess, and the
# way that proof fails is NOT a wrong label -- it is a MISSING one. The day
# gBASIC gains a twelfth network module and the table does not know, a library
# using it scans as harmless and every scan after that is silently incomplete.
#
# So this suite's load-bearing tier is COVERAGE, in BOTH directions, against the
# two lists the interpreter already maintains for its own reasons:
#
#   library_is_native_qualifier (src/eval.c)  -- maintained because alias
#       collision refusal depends on it, so it cannot quietly rot
#   dispatch_only[] (src/builtins.c)          -- maintained because has_builtin
#       depends on it, which is what made THREE missing names visible on
#       2026-09-30
#
# Deriving from lists that something else already needs is the same argument
# eval_module_needs_load's own comment makes, and the same one run_docs_gate's
# has_builtin tier makes: a list kept only for this purpose is a list that rots.
. "$(dirname "$0")/portable.sh"
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

status=0
checks=0
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

ok()  { checks=$((checks + 1)); printf 'ok   %s\n' "$1"; }
bad() { checks=$((checks + 1)); printf 'MISMATCH %s\n  %s\n' "$1" "$2"; status=1; }

./gbasic --capabilities >"$tmp/reported" 2>/dev/null || true
cut -f1 "$tmp/reported" | sort >"$tmp/names"

# --- the two source lists, read out of the C ---------------------------------
sed -n '/static int library_is_native_qualifier/,/^}/p' src/eval.c \
  | grep -oE '"[a-z_]+"' | tr -d '"' | sort -u >"$tmp/qualifiers"
sed -n '/static const char \*dispatch_only\[\]/,/};/p' src/builtins.c \
  | grep -oE '"[a-z_]+"' | tr -d '"' | sort -u >"$tmp/verbs"

echo "--- TIER 1: the report is not empty, and the scrapers matched ---"
# A scraper that matches nothing reports a clean run, which is the failure mode
# every derive-from-source check in this tree has had to defend against.
for pair in "reported:30" "qualifiers:15" "verbs:9"; do
    f="${pair%%:*}"; floor="${pair##*:}"
    n="$(grep -c . "$tmp/$f" || true)"
    [ "${n:-0}" -ge "$floor" ] \
        && ok "$f has $n entries (floor $floor)" \
        || bad "$f has only $n entries" "the scraper stopped matching, it did not pass"
done

echo "--- TIER 2: COVERAGE, both directions (the load-bearing tier) ---"
missing="$(comm -23 "$tmp/qualifiers" "$tmp/names" || true)"
[ -z "$missing" ] \
    && ok "every native qualifier has a capability classification" \
    || bad "native qualifiers with NO classification" "$(printf '%s' "$missing" | tr '\n' ' ')-- add them to gb_capability_table in src/eval.c; until then a library using one scans as harmless"
extra="$(comm -13 "$tmp/qualifiers" "$tmp/names" | grep -vxF -f "$tmp/verbs" | grep -vxE 'make_dir|atomic_replace|send|receive|self|spawn' || true)"
[ -z "$extra" ] \
    && ok "the table names nothing that is not a real qualifier, verb or actor primitive" \
    || bad "table names something unknown" "$(printf '%s' "$extra" | tr '\n' ' ')"

vmissing="$(comm -23 "$tmp/verbs" "$tmp/names" || true)"
[ -z "$vmissing" ] \
    && ok "every file/dir verb has a capability classification" \
    || bad "file verbs with NO classification" "$(printf '%s' "$vmissing" | tr '\n' ' ')"

echo "--- TIER 3: every label is from the declared vocabulary ---"
# Catches a typo (`nett`), which would make a scanner's filter silently miss it.
bad_label="$(cut -f2 "$tmp/reported" | tr ' ' '\n' | sort -u \
  | grep -vxE 'process|net|net:listen|db|fs:read|fs:write|display|actors|ffi|-' || true)"
[ -z "$bad_label" ] \
    && ok "no label outside the vocabulary" \
    || bad "label outside the vocabulary" "$(printf '%s' "$bad_label" | tr '\n' ' ')"

echo "--- TIER 4: CONTROLS -- specific classifications that must be right ---"
# Without these, coverage is satisfied by labelling everything "-".
want() {
    checks=$((checks + 1))
    got="$(awk -F'\t' -v n="$1" '$1==n {print $2}' "$tmp/reported")"
    [ "$got" = "$2" ] && printf 'ok   %s -> %s\n' "$1" "$got" \
        || { printf 'MISMATCH %s: got [%s], want [%s]\n' "$1" "$got" "$2"; status=1; }
}
want process   "process"
want webclient "net"
want webserver "net:listen"
want sqlite    "db fs:write"
want write     "fs:write"
want read      "fs:read"
want spawn     "actors"
# THE ONE INSPECTION GOT WRONG. `xml` looks like a string parser; it carries
# xml.parse_file, xml.read and xml.reader, so it reads the filesystem. Pinned
# because the next person will make the same assumption.
want xml       "fs:read"
# `gi` can instantiate anything with a typelib, so its reach is NOT bounded by
# this table and the label has to say so.
want gi        "ffi display"
# And the control that stops "label everything dangerous": a module with no
# external effect reports none.
want money     "-"
want timer     "-"

echo
echo "--- TIER 5: ONE SENTENCE FOR AN UNKNOWN VERB, BUILT IN ONE PLACE ---"
# Nine module dispatchers answer a verb they do not have -- webclient, http,
# timer, webserver, sqlite, odbc, pg, gi, money -- and each used to build
# `invalid function call: odbc.vacuum` from its own snprintf. The text said the
# wrong thing for all nine: the problem is not that the CALL is invalid, it is
# that the NAME does not exist, and a sentence about the call sends a reader to
# check their arguments. Reported by the gbasic-books session 2026-10-02 against
# `is_money`; the same complaint one namespace over.
#
# A RULE RE-ESTABLISHED AT NINE CALL SITES IS ONE A TENTH MODULE LEAVES OUT,
# which is the lesson `first_raise_wins` produced from the other direction, so
# the format lives in `module_unknown_verb` and this tier fails if a second
# construction site appears. Sited HERE because this suite's subject is the
# module surface and it already enumerates the modules.
#
# TWO HALVES, and the second is what keeps the first from being decoration: the
# formatter must exist and be the ONLY builder of that text, AND the message
# must actually REACH a caller for every module -- a formatter nobody calls
# would satisfy a source grep perfectly.
checks=$((checks + 1))
builders="$(grep -c 'undefined function: %s\.%s' src/eval.c || true)"
if [ "$builders" = "1" ]; then
    echo "ok   the unknown-verb sentence is built in exactly one place"
else
    echo "MISMATCH unknown-verb text built in $builders places, want 1 -- route it through module_unknown_verb"
    status=1
fi
checks=$((checks + 1))
stale="$(grep -c '"invalid function call: [a-z]*\.%s"' src/eval.c || true)"
if [ "$stale" = "0" ]; then
    echo "ok   no module still builds the old sentence"
else
    echo "MISMATCH $stale module dispatchers still say 'invalid function call'"
    status=1
fi
# AND THE COUNT, because the two checks above have a measured blind spot: a
# hand-rolled copy that happens to produce the SAME TEXT from a different
# format string passes both (proven -- that perturbation was green). Counting
# the CALL SITES is the direct statement, and the expected number is the length
# of the module list this tier probes rather than a literal, so the two cannot
# disagree about how many modules there are.
checks=$((checks + 1))
mod_list="webclient http timer webserver sqlite odbc pg gi money"
want_calls="$(printf '%s\n' $mod_list | grep -c .)"
# One definition plus one call per module.
got_calls="$(( $(grep -c 'module_unknown_verb(' src/eval.c) - 1 ))"
if [ "$got_calls" = "$want_calls" ]; then
    echo "ok   all $want_calls module dispatchers call the shared formatter"
else
    echo "MISMATCH $got_calls dispatchers call module_unknown_verb, want $want_calls -- a module is building its own sentence"
    status=1
fi
# The behavioural half: ask each module for a verb nothing defines.
verb_tmp="$tmp/unknown_verb.bas"
for mod in $mod_list; do
    checks=$((checks + 1))
    case "$mod" in
        timer|money) printf '%s.definitely_not_a_verb()\n' "$mod" >"$verb_tmp" ;;
        *) printf 'load %s\n%s.definitely_not_a_verb()\n' "$mod" "$mod" >"$verb_tmp" ;;
    esac
    out="$(GBASIC_PATH=stdlib ./gbasic "$verb_tmp" 2>&1 || true)"
    case "$out" in
        *"undefined function: $mod.definitely_not_a_verb"*)
            printf 'ok   %s names the verb it does not define\n' "$mod" ;;
        # The third spelling is gi's ("gobject-introspection support is
        # unavailable; install ..."), the one tests/build_has.sh already
        # counts as a build refusal; without it any build lacking GI -- Windows,
        # or a lean Linux build -- failed here rather than skipping.
        *"not available in this build"*|*"was not built with"*|*"support is unavailable"*)
            printf 'ok   %s (compiled out; nothing to ask)\n' "$mod" ;;
        *)
            printf 'MISMATCH %s: [%s]\n' "$mod" "$(printf '%s' "$out" | tail -1)"
            status=1 ;;
    esac
done

# ---------------------------------------------------------------------------
# `has_builtin` MUST ANSWER FOR THIS BUILD, which until 0.7.1 it did not.
#
# THE DEFECT, MEASURED ON THE PUBLISHED 0.7.0 ARTIFACTS. `has_builtin` is
# documented as the way a program degrades gracefully across releases, and on
# two of the three published artifacts it could not answer the question it
# exists for: it affirmed `password_hash`, `sha256` and `compress` on builds
# that cannot run any of them. So a program that CHECKED FIRST and then called
# crashed on the branch it had checked for -- which is worse than DOGFOOD 46,
# the same defect inverted, where a careful program merely took a weaker path.
# `compress` was the sharpest: `has_builtin` said true and the same binary
# answered `unknown function: compress`, because zlib was the one optional
# builtin family whose NAME TEST sat inside its `#if`, so the function vanished
# rather than refusing. Reported by the books session.
#
# THE INVARIANT IS BUILD-AGNOSTIC, which is what makes it testable here rather
# than only in a release: for every builtin that can depend on the build,
# `has_builtin(n)` is true EXACTLY WHEN calling it does not refuse for
# unavailability. That holds on a full build, a lean one and everything
# between, so this tier does not need a second compile to be meaningful -- and
# it fails in both directions, which matters because the two defects above
# point opposite ways.
#
# WHAT THIS TIER DOES NOT COVER, AND SAYS SO: it runs against whatever binary
# this suite built, and the suite builds everything, so on this machine every
# probe answers "available and has_builtin agrees". That is still a real check
# -- the invariant fails in both directions and a regression in either shows up
# here -- but the case the defect SHIPPED IN is a build with the dependency
# absent, and only a lean build exercises it. That half is measured by
# tools/build-release-tarball.sh's library sweep and by run_platform's lean
# build; the perturbations were proven against a real lean compile by hand:
# unguarding the registry reproduces `has_builtin` affirming all three, and
# moving zlib's name test back inside its `#if` reproduces `compress`
# vanishing. The vanishing diagnostic ends with "has_builtin(\"compress\") asks
# without raising", which on the shipped binary answered TRUE -- the message
# pointed at the probe that was lying.
#
# THE ARITY TABLE IS HAND-WRITTEN AND THE COVERAGE CHECK IS WHAT KEEPS IT
# HONEST: a probe needs a call the arity check will let through, and a table
# kept only for this purpose is a list that rots -- this suite's own header
# says so. So every name guarded in src/builtins.c must appear below, and a new
# optional builtin cannot be added to the registry without a probe for it.
printf '\n'
probe_dir="$tmp/hb"; mkdir -p "$probe_dir"
hb_probe() {   # $1 = name, $2 = argument list
    printf 'on error goto next\nx = %s(%s)\nif error then\n  print error.message\n  error.clear()\nelse\n  print "WORKS"\nend if\n' "$1" "$2" > "$probe_dir/c.bas"
    GBASIC_PATH=stdlib ./gbasic "$probe_dir/c.bas" 2>&1 | head -1
}
hb_says() {
    printf 'print string(has_builtin("%s"))\n' "$1" > "$probe_dir/h.bas"
    GBASIC_PATH=stdlib ./gbasic "$probe_dir/h.bas" 2>&1 | head -1
}

# name:arguments -- one row per builtin whose availability depends on the build
HB_TABLE='
sha256:"abc"
sha512:"abc"
sha1:"abc"
md5:"abc"
hmac_sha256:"k","m"
hmac_sha1:"k","m"
hmac_sha512:"k","m"
aes_gcm_encrypt:"0123456789abcdef0123456789abcdef","0123456789ab","p","a"
aes_gcm_decrypt:"0123456789abcdef0123456789abcdef","0123456789ab","c","a"
ed25519_keypair:
ed25519_sign:"sk","m"
ed25519_verify:"pk","m","sig"
pbkdf2_sha256:"p","s",1000,16
pbkdf2_sha512:"p","s",1000,16
scrypt:"p","s",16384,8,1,32
password_hash:"pw"
password_verify:"pw","$y$j9T$x$y"
password_hash_cost:
compress:"hello"
uncompress:"hello"
'
hb_checked=0
for row in $HB_TABLE; do
    nm="${row%%:*}"
    args="${row#*:}"
    msg="$(hb_probe "$nm" "$args")"
    says="$(hb_says "$nm")"
    case "$msg" in
        *"requires OpenSSL"*|*"requires zlib"*|*"not available in this build"*|*"support is unavailable"*|*"rebuild with it installed"*)
            unavailable=1 ;;
        *"unknown function"*|*"undefined function"*)
            # THE SELF-CONTRADICTION THIS TIER WAS WRITTEN FOR. A name the
            # dispatch does not know is not a clean refusal, whatever
            # has_builtin says, so it fails outright rather than being
            # classified.
            bad "$nm vanished from the dispatch instead of refusing" "$msg"
            hb_checked=$((hb_checked + 1)); continue ;;
        *) unavailable=0 ;;
    esac
    hb_checked=$((hb_checked + 1))
    if [ "$unavailable" = 1 ] && [ "$says" = "true" ]; then
        bad "has_builtin($nm) says true on a build that refuses it" "$msg"
    elif [ "$unavailable" = 0 ] && [ "$says" != "true" ]; then
        bad "has_builtin($nm) says $says on a build that runs it" "$msg"
    else
        ok "has_builtin($nm) = $says, and the call agrees"
    fi
done
if [ "$hb_checked" -lt 18 ]; then
    bad "only $hb_checked optional builtins were probed" "the table has shrunk; it is measuring almost nothing"
fi

# COVERAGE, BOTH DIRECTIONS, against the registry's own guards.
guarded="$(awk '
  /^#if +(HAVE_LIBCRYPTO|HAVE_ZLIB|HAVE_LIBXCRYPT|HAVE_LIBXCRYPT \|\| HAVE_YESCRYPT)/ { on=1; next }
  /^#endif/ { on=0; next }
  on && match($0, /"[a-z_0-9]+"/) { print substr($0, RSTART+1, RLENGTH-2) }
' src/builtins.c | sort -u)"
if [ -z "$guarded" ]; then
    bad "read no guarded names out of src/builtins.c" "the registry's guards or this scan have moved"
else
    uncovered=""
    for g in $guarded; do
        printf '%s' "$HB_TABLE" | command grep -q "^$g:" || uncovered="$uncovered $g"
    done
    if [ -n "$uncovered" ]; then
        bad "guarded in the registry with no probe here:$uncovered" "add a row to HB_TABLE"
    else
        ok "every one of the $(printf '%s\n' $guarded | wc -l | tr -d ' ') registry-guarded builtins has a probe"
    fi
fi

printf '\nchecks: %d\n' "$checks"
if [ "$status" = 0 ]; then printf 'mismatches: 0\n'; else printf 'FAILED\n'; fi
exit "$status"
