#!/usr/bin/env bash
# `gbasic --check FILE` (2026-11-26, the third AI aid): parse FILE and every
# library it loads, make every check that comes before a statement runs, and
# RUN NOTHING. Each diagnostic is a --json-diagnostics line on stdout; none
# means clean; an error exits 1. The agent's write, check, fix loop.
#
# Tiers:
#   nothing   a program that prints, writes a file, starts a process and
#             exits 3 leaves no trace under --check: no output, no file, exit
#             0. With a program block and without one.
#   reports   by hand: a parse error; a library that does not parse (its own
#             path); a load that resolves nowhere, at the top and inside the
#             block; a function defined twice; warning 2106 and note 2102,
#             each exiting 0; the usage refusing a program argument.
#   agree     for each error a run reports before its first statement, the
#             lines --check prints are the lines --json-diagnostics prints.
#   stdlib    every library in stdlib/ checks clean.
#   planted   the fix field's oracle: a mistake planted in real files from
#             the tree (tests/check/plant.py: <>, elseif, endif, let, a mod
#             b) must be the first diagnostic, carry a fix, and the fix,
#             applied from its printed numbers alone, must give back the
#             original file byte for byte. Skips without python3.
# Headless, no network.
. "$(dirname "$0")/portable.sh"
set -u
cd "$(dirname "$0")/.."
make >/dev/null 2>&1 || { echo "FAIL run_check: build failed"; exit 1; }

status=0
pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; status=1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
G="$(pwd)/gbasic"

echo "--- TIER nothing ---"
cat >"$work/acts.bas" <<'EOF'
print "ran"
write("made.txt", "x")
p = process.run({ command: "touch", args: ["touched.txt"] })
exit(3)
EOF
cat >"$work/acts_block.bas" <<'EOF'
function helper()
    return 1
end function
program main(args)
    print "ran"
    write("made.txt", "x")
    p = process.run({ command: "touch", args: ["touched.txt"] })
    exit(3)
end program
EOF
for f in acts acts_block; do
    # the control: run, it acts
    (cd "$work" && rm -f made.txt touched.txt && "$G" $f.bas >/dev/null 2>&1; echo $? >rc)
    if [ "$(cat "$work/rc")" = 3 ] && [ -f "$work/made.txt" ] && [ -f "$work/touched.txt" ]; then
        :
    else
        fail "$f: the control did not act when run (exit $(cat "$work/rc"))"
        continue
    fi
    out=$(cd "$work" && rm -f made.txt touched.txt && "$G" --check $f.bas 2>&1; echo "exit $?")
    if [ "$out" = "exit 0" ] && [ ! -e "$work/made.txt" ] && [ ! -e "$work/touched.txt" ]; then
        pass "$f: run, it prints, writes, starts a process and exits 3; checked, nothing"
    else
        fail "$f under --check: $out; made.txt $( [ -e "$work/made.txt" ] && echo exists || echo absent)"
    fi
done

echo "--- TIER reports ---"
expect() {   # expect <label> <file> <exit> <substring>...
    local label=$1 f=$2 want=$3 out rc
    shift 3
    out=$(cd "$work" && "$G" --check "$f" 2>"$work/stderr")
    rc=$?
    if [ "$rc" != "$want" ]; then fail "$label: exit $rc, wanted $want: $out"; return; fi
    if [ -s "$work/stderr" ]; then fail "$label: wrote to stderr: $(cat "$work/stderr")"; return; fi
    for s in "$@"; do
        case "$out" in *"$s"*) ;; *) fail "$label: lacks $s: $out"; return ;; esac
    done
    pass "$label"
}
w() { printf '%b' "$2" >"$work/$1"; }
w parse.bas 'x = 1\nif x = 1 then\n    print "a"\n'
expect "a parse error" parse.bas 1 '"severity":"error"' '"code":"GB_DIAG_PARSE_ERROR"' '"path":"parse.bas"'
w bad.bas 'library bad\n    function g(\nend library\n'
w badlib.bas 'load bad from "bad.bas"\nprint "ran"\n'
expect "a library that does not parse, at its own path" badlib.bas 1 '"path":"./bad.bas","start":{"line":3' 'could not parse library file'
w nolib.bas 'load nosuchlib\nprint "ran"\n'
expect "a load that resolves nowhere" nolib.bas 1 '"subcode":1003' 'library not found: nosuchlib'
w nolib_block.bas 'program main(args)\n    load nosuchlib\n    print "ran"\nend program\n'
expect "and inside the program block, at its line" nolib_block.bas 1 '"start":{"line":2,"column":5}' 'library not found: nosuchlib'
w twice.bas 'function f()\n    return 1\nend function\nfunction f()\n    return 2\nend function\nprint f()\n'
expect "a function defined twice" twice.bas 1 '"start":{"line":4' 'defined twice'
w dead.bas 'x = 1\nprogram main(args)\n    print x\nend program\n'
expect "warning 2106, exiting 0" dead.bas 0 '"severity":"warning"' '"subcode":2106'
w shadow.bas 'function len(x)\n    return 1\nend function\nprint len("ab")\n'
expect "note 2102 through the sink, exiting 0" shadow.bas 0 '"severity":"note"' '"subcode":2102'
w stdlib_use.bas 'load dates\nload net\nprint dates.days_between({date}"2026-01-01", {date}"2026-01-03")\n'
expect "a stdlib load and a module load check clean" stdlib_use.bas 0
if "$G" --check "$work/parse.bas" extra >/dev/null 2>&1; then
    fail "--check took a program argument it cannot use"
else
    pass "--check refuses a program argument (it runs nothing)"
fi

echo "--- TIER agree ---"
agreed=0
for f in parse badlib nolib nolib_block twice; do
    a=$(cd "$work" && "$G" --check $f.bas 2>/dev/null)
    b=$(cd "$work" && "$G" --json-diagnostics $f.bas 2>&1 >/dev/null)
    if [ "$a" = "$b" ]; then agreed=$((agreed + 1)); else fail "agree $f:\n  --check:            $a\n  --json-diagnostics: $b"; fi
done
[ "$agreed" = 5 ] && pass "5 programs: --check prints what --json-diagnostics reports"

echo "--- TIER stdlib ---"
bad=""
n=0
for f in stdlib/*.bas; do
    n=$((n + 1))
    out=$(GBASIC_PATH=stdlib "$G" --check "$f" 2>&1) || bad="$bad $f"
    [ -z "$out" ] || bad="$bad $f"
done
if [ -z "$bad" ] && [ "$n" -ge 50 ]; then pass "$n libraries in stdlib/ check clean"; else fail "stdlib not clean:$bad"; fi

echo "--- TIER planted ---"
if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP planted (no python3)\n'
else
    # a fix's numbers applied to a file, as an agent would
    apply='
import json, sys
src = open(sys.argv[1], "rb").read()
d = json.loads(open(sys.argv[2]).readline())
f = d["fix"]
lines = src.split(b"\n")
off = lambda p: sum(len(l) + 1 for l in lines[:p["line"] - 1]) + p["column"] - 1
sys.stdout.buffer.write(src[:off(f["start"])] + f["text"].encode() + src[off(f["end"]):])
'
    ls stdlib/*.bas tests/*.bas examples/*.bas | sort >"$work/corpus"
    for kind in ne elseif endif let mod; do
        planted=0; restored=0; wrong=""
        while read -r f; do
            [ -f "${f%.bas}.err" ] && continue
            line=$(python3 tests/check/plant.py "$kind" "$f" "$work/p.bas") || continue
            planted=$((planted + 1))
            "$G" --check "$work/p.bas" >"$work/p.json" 2>/dev/null
            first=$(head -1 "$work/p.json")
            case "$first" in
                *"\"start\":{\"line\":$line,"*'"fix":'*) ;;
                *) wrong="$wrong $f:$line"; continue ;;
            esac
            python3 -c "$apply" "$work/p.bas" "$work/p.json" >"$work/fixed.bas"
            if cmp -s "$work/fixed.bas" "$f"; then restored=$((restored + 1)); else wrong="$wrong $f:$line(apply)"; fi
            [ "$planted" -ge 40 ] && break
        done <"$work/corpus"
        if [ -z "$wrong" ] && [ "$restored" -ge 5 ]; then
            pass "$kind: planted in $planted files of the tree; each the first diagnostic, its fix giving back the file byte for byte"
        else
            fail "$kind: $restored of $planted restored; wrong:$wrong"
        fi
    done
fi

[ "$status" = 0 ] && echo "PASS tests/run_check.sh" || echo "FAIL tests/run_check.sh"
exit $status
