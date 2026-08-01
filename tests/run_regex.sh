#!/usr/bin/env bash
# TEXT-0: regex in the core (docs/text_design.md §2-§3, decisions §13.D-G).
#
# The surface is a `regex` VALUE KIND plus overloads of verbs that already
# exist -- contains / replace / split -- rather than a parallel set of re_*
# builtins. Two verbs carry new names (match, match_all) because their return
# shape has no literal counterpart: a literal find yields one index, a regex
# match must yield text/start/length/groups.
#
# Tiers:
#   1. GOLDEN -- tests/regex_test.bas. Every overload is exercised in BOTH
#      modes on input where the literal and pattern answers DIFFER, so an
#      overload that silently swallowed the literal case moves the golden
#      instead of passing quietly. Also pins codepoint offsets composing with
#      mid, unknown-on-miss, the unknown-vs-"" group distinction, zero-width
#      match termination, and binary safety across an interior NUL.
#   2. FLAG MATRIX -- the one part of this work that is not a direct mapping
#      onto libc. POSIX couples "dot matches newline" and "^/$ match at line
#      boundaries" into the single REG_NEWLINE bit; gBASIC separates them. Two
#      of the four combinations are therefore unreachable without rewriting the
#      pattern, and REG_NEWLINE additionally stops [^x] matching a newline,
#      which PCRE's /m does not. This tier asserts all four combinations for
#      `.`, for `[^x]`, and for ^/$ -- 12 assertions that would each pass
#      individually under a wrong REG_NEWLINE choice but cannot all pass
#      together. Without it the translation table in eval.c is unguarded.
#   3. NEGATIVE -- the error paths, each pinned to its message: an unknown
#      flag, an uncompilable pattern, \D inside a bracket expression (POSIX has
#      no negated class there, so it is rejected rather than mistranslated), an
#      interior NUL in a PATTERN (rejected -- regcomp cannot honor it -- while
#      a NUL in a SUBJECT works, which tier 1 proves), and the type errors.
#   4. ACTOR ROUND-TRIP -- a regex value crossing a process boundary. It ships
#      as pattern+flags and the receiver recompiles, because a regex_t holds
#      internal pointers and is meaningless in another address space.
#   5. VALGRIND -- the golden under valgrind. A refcounted value kind with a
#      regfree() in its release path is exactly where a leak or double-free
#      would hide.
#
# Headless, GI-independent, no display, no network. Never skips (bar valgrind).
set -u

cd "$(dirname "$0")/.."

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_regex: build failed\n'
    exit 1
fi

out=$(mktemp)
err=$(mktemp)
tmp=$(mktemp -d)
trap 'rm -rf "$out" "$err" "$tmp"' EXIT

status=0

# --- Tier 1: golden -----------------------------------------------------------
printf -- '-- golden\n'
if timeout 120 ./gbasic tests/regex_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u tests/regex_test.out "$out"; then
        printf 'PASS tests/regex_test.bas\n'
    else
        printf 'FAIL tests/regex_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL tests/regex_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# --- Tier 2: the flag matrix --------------------------------------------------
# Each row: flags, then the expected true/false for the three probes.
#
#   dot    a.b      against "a\nb"   -- does `.` match a newline?
#   negbr  a[^x]b   against "a\nb"   -- does a negated bracket match a newline?
#   caret  ^second$ against "first\nsecond" -- are ^/$ line anchors?
#
# Expected values are PCRE semantics, which is what the surface promises:
#   "s" governs `.` alone; "m" governs ^/$ alone; neither touches [^x], which
#   always matches a newline.
printf -- '-- flag matrix (POSIX couples what gBASIC separates)\n'
cat >"$tmp/flags.bas" <<'EOF'
program main(args)
  f = args[0]
  nl = "a" + "\n" + "b"
  two = "first" + "\n" + "second"
  if f = "-" then
    print "dot=" + contains(nl, regex("a.b"))
    print "negbr=" + contains(nl, regex("a[^x]b"))
    print "caret=" + contains(two, regex("^second$"))
  else
    print "dot=" + contains(nl, regex("a.b", f))
    print "negbr=" + contains(nl, regex("a[^x]b", f))
    print "caret=" + contains(two, regex("^second$", f))
  end if
end program
EOF

flag_case() { # flags expect_dot expect_negbr expect_caret
    local f=$1 want="dot=$2
negbr=$3
caret=$4"
    if ! timeout 60 ./gbasic "$tmp/flags.bas" "$f" >"$out" 2>"$err" </dev/null; then
        printf 'FAIL flags "%s" (exit)\n' "$f"
        cat "$err"
        status=1
        return
    fi
    if [ "$(cat "$out")" = "$want" ]; then
        printf 'PASS flags %-4s dot=%-5s negbr=%-5s caret=%s\n' "\"$f\"" "$2" "$3" "$4"
    else
        printf 'FAIL flags "%s"\n  want: %s\n  got:  %s\n' \
               "$f" "$(echo "$want" | tr '\n' ' ')" "$(tr '\n' ' ' <"$out")"
        status=1
    fi
}

#                     .       [^x]    ^/$
flag_case "-"  false   true    false   # default: dot excludes \n, ^/$ anchor string
flag_case "s"  true    true    false   # s: dot admits \n
flag_case "m"  false   true    true    # m: ^/$ per line, dot still excludes \n
flag_case "ms" true    true    true    # both, independently

# --- Tier 3: negative ---------------------------------------------------------
printf -- '-- negative (each error pinned to its message)\n'
negative() { # label source expected-substring
    local label=$1 src=$2 want=$3
    printf '%s\n' "$src" >"$tmp/neg.bas"
    if timeout 60 ./gbasic "$tmp/neg.bas" >"$out" 2>"$err" </dev/null; then
        printf 'FAIL negative %-22s (expected a raise, program succeeded)\n' "$label"
        status=1
        return
    fi
    if grep -qF "$want" "$err"; then
        printf 'PASS negative %-22s %s\n' "$label" "$want"
    else
        printf 'FAIL negative %-22s\n  want substring: %s\n  got: %s\n' \
               "$label" "$want" "$(cat "$err")"
        status=1
    fi
}

negative "unknown flag" \
    'program main(args)
  print regex("a", "q")
end program' \
    "regex: unknown flag 'q'"

negative "uncompilable pattern" \
    'program main(args)
  print regex("a[")
end program' \
    "regex:"

negative "negated class in bracket" \
    'program main(args)
  print regex("[\\D]")
end program' \
    "have no meaning inside a"

negative "NUL in pattern" \
    'program main(args)
  print regex(from_bytes([65, 0, 66]))
end program' \
    "cannot contain an interior NUL"

negative "match on non-string" \
    'program main(args)
  print match(42, "a")
end program' \
    "first argument must be a string"

negative "bad pattern type" \
    'program main(args)
  print match("abc", 42)
end program' \
    "must be a string or a regex value"

negative "flags with compiled pattern" \
    'program main(args)
  print match("abc", regex("a"), "i")
end program' \
    "flags belong to regex()"

negative "contains bad needle" \
    'program main(args)
  print contains("abc", 42)
end program' \
    "expects a string or regex needle"

# --- Tier 4: actor round-trip -------------------------------------------------
# A regex crossing a process boundary must arrive matching identically. It
# cannot travel as a compiled program (regex_t holds internal pointers), so this
# proves the pattern+flags representation actually reconstitutes.
printf -- '-- actor round-trip (recompiled on the far side)\n'
cat >"$tmp/actor.bas" <<'EOF'
' Mailbox loopback: the value is serialized, crosses a real socket, and is
' deserialized -- the same path a spawned actor's message takes.
send(self(), regex("hello", "i"))
r = receive()
' It must BEHAVE as a regex on arrival, not merely survive as some value: the
' flags have to have made the trip too, which the case-insensitive hit proves.
print("round-trip=" + type(r) + "," + contains("HELLO", r) + "," + contains("nope", r) + "," + (r = regex("hello", "i")))
EOF
if timeout 60 ./gbasic "$tmp/actor.bas" >"$out" 2>"$err" </dev/null; then
    if [ "$(cat "$out")" = "round-trip=regex,true,false,true" ]; then
        printf 'PASS actor round-trip\n'
    else
        printf 'FAIL actor round-trip: %s\n' "$(cat "$out")"
        status=1
    fi
else
    printf 'FAIL actor round-trip (exit)\n'
    cat "$err"
    status=1
fi

# --- Tier 5: valgrind ---------------------------------------------------------
if command -v valgrind >/dev/null 2>&1; then
    if valgrind --error-exitcode=9 --leak-check=full --track-fds=yes \
                --errors-for-leak-kinds=definite \
                ./gbasic tests/regex_test.bas >"$out" 2>"$err" </dev/null; then
        if diff -q tests/regex_test.out "$out" >/dev/null; then
            printf 'PASS valgrind tests/regex_test.bas\n'
        else
            printf 'FAIL valgrind tests/regex_test.bas (output differs under valgrind)\n'
            status=1
        fi
    else
        printf 'FAIL valgrind tests/regex_test.bas\n'
        cat "$err"
        status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit "$status"
