#!/usr/bin/env bash
# Shapes, a record's declared form (docs/shapes_design.md), increment 1: the
# block, instances, fixed fields, writes, methods and shaped fields; and
# increment 2: a shape's name as a lock (§5).
#
# Tiers:
#   golden    tests/shapes/instances.bas against instances.out, and
#             tests/shapes/locks.bas against locks.out, every line of which was
#             worked out by hand from the design before the implementation ran;
#             interpreted and compiled (GBASIC_COMPILE=all) both.
#   locks     a lock naming a shape refusing: a value that is not a record (a
#             variable, a parameter, a return, an element by its number), a
#             field that does not convert (named in its own words after whose
#             lock refused), a lock naming no declared shape refused BEFORE
#             anything runs (with "did you mean" for a type or a shape), a
#             script locking above its shape, --check, the prompt.
#   lockwarn  the two warnings through a variable, a parameter and an element:
#             once per field, a default silent; `on warning stop` refuses in
#             the warning's own words.
#   refusals  each refusal by its message: a field the shape does not declare
#             (written, by brackets, in `with`), a value its type refuses, a
#             method written as a field or attached outside, a shape declared
#             twice, inside a function or a library, with no fields, with a
#             constructor, a shaped field given a non-record, a reference to
#             no shape. Each beside the program that works, so a refusal of
#             everything cannot pass.
#   warnings  converting a record into a shape: an extra field dropped (2126),
#             a field with no default missing (2127), a field WITH a default
#             missing taking it silently; `on warning stop` refuses.
#   parse     what the grammar refuses: another language's type, a statement
#             in the body, a default that is not a literal (a shape is
#             hoisted, and the hoist runs no user code), the wrong `end`, a
#             dotted method; --ast; zero grammar conflicts.
#   hoist     a shape beside `program main` used inside it, and under --check.
#   valgrind  the golden and every refusal.
. "$(dirname "$0")/portable.sh"
set -u
cd "$(dirname "$0")/.."
make >/dev/null 2>&1 || { echo "FAIL run_shapes: build failed"; exit 1; }

status=0
pass() { printf 'ok   %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; status=1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
G="$(pwd)/gbasic"

echo "--- TIER golden ---"
if "$G" tests/shapes/instances.bas 2>"$work/err" | cmp -s - tests/shapes/instances.out && [ ! -s "$work/err" ]; then
    pass "instances.bas prints the hand-worked golden, nothing on stderr"
else
    fail "instances.bas: $(diff <("$G" tests/shapes/instances.bas 2>&1) tests/shapes/instances.out | head -5)"
fi
if GBASIC_COMPILE=all "$G" tests/shapes/instances.bas 2>/dev/null | cmp -s - tests/shapes/instances.out; then
    pass "and compiled, the same bytes"
else
    fail "instances.bas compiled differs"
fi
if "$G" tests/shapes/locks.bas 2>"$work/err" | cmp -s - tests/shapes/locks.out && [ ! -s "$work/err" ]; then
    pass "locks.bas prints the hand-worked golden, nothing on stderr"
else
    fail "locks.bas: $(diff <("$G" tests/shapes/locks.bas 2>&1) tests/shapes/locks.out | head -5)"
fi
if GBASIC_COMPILE=all "$G" tests/shapes/locks.bas 2>/dev/null | cmp -s - tests/shapes/locks.out; then
    pass "and compiled, the same bytes"
else
    fail "locks.bas compiled differs"
fi

HEAD='shape account\n    owner:text = "unnamed"\n    balance:money(USD) = "0.00"\n    opened:date\n    function deposit(n)\n        this.balance += n\n        return this.balance\n    end function\nend shape\n'
refuse() {   # refuse <label> <file> <source> <message substring>
    printf '%b' "$3" >"$work/$2"
    local out rc
    out=$(cd "$work" && "$G" "$2" 2>&1)
    rc=$?
    case "$out" in
        *"$4"*) [ "$rc" != 0 ] && pass "$1" || fail "$1: exit 0: $out" ;;
        *) fail "$1: wanted: $4  got: $(printf '%s' "$out" | head -2)" ;;
    esac
}
runs() {   # runs <label> <source> <stdout>
    printf '%b' "$2" >"$work/r.bas"
    local out
    out=$(cd "$work" && "$G" r.bas 2>&1)
    [ "$out" = "$(printf '%b' "$3")" ] && pass "$1" || fail "$1: printed [$out]"
}

echo "--- TIER refusals ---"
runs "the working neighbour: a declared field written" "${HEAD}a = new account\na.owner = \"A\"\nprint a.owner\n" 'A'
refuse "a field the shape does not declare" f1.bas "${HEAD}a = new account\na.nickname = \"A\"\n" 'account has no field nickname (account is declared at line 1)'
refuse "the same, by brackets" f2.bas "${HEAD}a = new account\na[\"nickname\"] = 1\n" 'account has no field nickname'
refuse "the same, in with" f3.bas "${HEAD}a = new account with { nickname: \"A\" }\n" 'account has no field nickname'
refuse "a value the field's type refuses, naming the field and its line" f4.bas "${HEAD}a = new account\na.balance = \"lots\"\n" 'balance is locked to money by balance:money(USD) at line 3'
refuse "and in with" f5.bas "${HEAD}a = new account with { opened: \"soon\" }\n" 'opened is locked to date by opened:date at line 4'
refuse "a method written as a field" f6.bas "${HEAD}a = new account\na.deposit = 0\n" 'deposit is a method of account, not a field'
refuse "a method attached outside the block" f7.bas "${HEAD}function account.extra()\n    return 1\nend function\n" "a shape's methods are declared inside \`shape account\`"
refuse "a shape declared twice" f8.bas "${HEAD}${HEAD}" 'shape account is declared twice: first at line 1'
refuse "a shape inside a function" f9.bas 'function f()\n    shape s\n        n:number\n    end shape\nend function\nf()\n' 'shape s is declared inside a function'
printf 'library lib\n    shape s\n        n:number\n    end shape\nend library\n' >"$work/lib.bas"
refuse "a shape in a library" f10.bas 'load lib from "lib.bas"\nprint 1\n' 'shapes are not exported from libraries yet'
refuse "a shape with no fields" f11.bas 'shape s\nend shape\n' 'shape s declares no fields'
refuse "a constructor, not in shapes yet" f12.bas 'shape s\n    n:number\n    function constructor()\n        return 1\n    end function\nend shape\n' 'a constructor is not in shapes yet'
refuse "a shaped field given a non-record" f13.bas 'shape p\n    name:text\nend shape\nshape s\n    h:p\nend shape\nx = new s\nx.h = 5\n' 'h is a p (s at line 5), and a number is not a record'
refuse "a reference to no shape" f14.bas 'shape s\n    h:nobody\nend shape\n' 'h:nobody names nobody, which is neither a data type nor a shape declared above this one'
refuse "a default its type refuses, at the field's line" f15.bas 'shape s\n    label:text\n    n:number = "abc"\nend shape\n' 'f15.bas:3:5: n is locked to number by n:number at line 3'
refuse "a field declared twice" f16.bas 'shape s\n    n:number\n    n:text\nend shape\n' 'shape s declares n twice (lines 2 and 3)'
runs "a method's += converts text, as a lock does" "${HEAD}a = new account\nprint a.deposit(\"2.50\")\nprint a.deposit(\"1.25\")\n" '2.50\n3.75'
runs "a shape naming itself (a linked list), and a keyword field name" 'shape node\n    value:number = 0\n    next:node = nothing\nend shape\na = new node with { value: 1 }\na.next = { value: "2" }\nprint a.next.value + 1\n' '3'

echo "--- TIER locks ---"
LH='shape account\n    owner:text = "unnamed"\n    balance:money(USD) = "0.00"\nend shape\n'
runs "the working neighbour: a record converts through the lock" "${LH}a:account = { owner: \"A\", balance: \"1\" }\nprint a.balance\n" '1.00'
refuse "a variable given a number" l1.bas "${LH}a:account = 5\n" 'a is locked to account by a:account at line 5, and this value does not convert: a number is not a record, and only a record converts into shape account'
refuse "a field that does not convert, in its own words" l2.bas "${LH}a:account = { balance: \"lots\" }\n" 'a:account at line 5, and this value does not convert: balance is locked to money by balance:money(USD) at line 3'
refuse "a parameter, refused at the call" l3.bas "${LH}function f(e:account)\n    return e\nend function\nprint f([1])\n" 'l3.bas:8:1: e is locked to account by e:account at line 5, and this value does not convert: an array is not a record'
refuse "a return, refused at its return" l4.bas "${LH}function f():account\n    return \"x\"\nend function\nprint f()\n" 'l4.bas:6:5: f returns account, declared by f(...):account at line 5'
refuse "an element, by its number" l5.bas "${LH}for each r:account in [{ owner: \"a\" }, 7]\n    print r.owner\nend for\n" 'r is locked to account by r:account at line 5, and element 2 does not convert'
refuse "a plain = later, as any lock" l6.bas "${LH}a:account = {}\na = \"x\"\n" 'a is locked to account by a:account at line 5, and this value does not convert: a string is not a record'
refuse "a field the shape lacks, written through the lock" l7.bas "${LH}a:account = {}\na.nick = 1\n" 'account has no field nick'
refuse "a misspelled type, before anything runs" l8.bas "print 1\nx:nubmer = 1\n" "l8.bas:2:3: 'nubmer' is neither a data type nor a shape this file declares; did you mean number?"
refuse "a misspelled shape" l9.bas "print 1\n${LH}x:acount = {}\n" "did you mean account?"
refuse "no such type or shape" l10.bas "print 1\nfunction f(x:zebra)\nend function\n" "'zebra' is neither a data type nor a shape this file declares. The types are number"
refuse "a script locking above its shape" l11.bas "a:account = {}\n${LH}" 'account is not a shape yet: without a `program` block a shape is declared when its line is reached'
for f in l8 l9 l10; do
    if [ -s "$work/$f.bas" ] && (cd "$work" && "$G" "$f.bas" 2>/dev/null) | grep -q .; then
        fail "$f.bas printed before its refusal"
    fi
done
pass "a lock naming no shape is refused before the first statement runs"
printf 'program main(args)\n    a:acount = {}\nend program\nshape account\n    n:number = 1\nend shape\n' >"$work/c1.bas"
out=$("$G" --check "$work/c1.bas"); rc=$?
case "$out" in *'"code":"GB_DIAG_PARSE_ERROR"'*'did you mean account?'*) [ $rc = 1 ] && pass "--check reports it, a shape declared below" || fail "--check exit $rc" ;; *) fail "--check: $out" ;; esac
out=$(printf 'shape s\n    n:number = 1\nend shape\na:s = { n: "4" }\nprint a.n + 1\nfunction f(x:s)\n    return x.n\nend function\nprint f({})\n' | "$G" --repl 2>&1)
[ "$out" = "$(printf '5\n1')" ] && pass "the prompt: a later line locks to a shape an earlier one declared" || fail "prompt: $out"

echo "--- TIER lockwarn ---"
WH='shape p\n    name:text\n    age:number = 0\n    born:date\nend shape\n'
printf '%b' "${WH}a:p = { name: \"A\", extra: 1 }\nfunction f(e:p)\n    return e.age\nend function\nprint f({ name: \"B\", born: \"2001-01-01\", more: 2 })\nfor each r:p in [{ name: \"C\" }, { name: \"D\" }]\n    print r.age\nend for\n" >"$work/lw.bas"
out=$(cd "$work" && "$G" lw.bas 2>"$work/lw.err"); rc=$?
if [ $rc = 0 ] && [ "$out" = "$(printf '0\n0\n0')" ] &&
   grep -q 'p has no field extra, so it was dropped.* at .*lw.bas:6:1 \[2126\]' "$work/lw.err" &&
   grep -q 'p.born was not given and has no default, so it is nothing.* at .*lw.bas:6:1 \[2127\]' "$work/lw.err" &&
   grep -q 'p has no field more, so it was dropped.* at .*lw.bas:10:1 \[2126\]' "$work/lw.err" &&
   grep -q 'p.born was not given.* at .*lw.bas:11:1 \[2127\]' "$work/lw.err" &&
   ! grep -q 'p.age\|p.name' "$work/lw.err" && [ "$(wc -l <"$work/lw.err")" = 4 ]; then
    pass "a variable, a parameter (at the call) and an element: dropped and missing said once each, defaults silent"
else
    fail "lockwarn: exit $rc, out [$out], stderr: $(cat "$work/lw.err")"
fi
printf '%b' "on warning stop\n${WH}a:p = { name: \"A\", born: \"2001-01-01\", extra: 1 }\nprint \"ran on\"\n" >"$work/lw2.bas"
out=$(cd "$work" && "$G" lw2.bas 2>&1)
case "$out" in *"ran on"*) fail "on warning stop let a lock drop a field: $out" ;; *"p has no field extra, so it was dropped"*) pass "on warning stop: a lock's drop refused in the warning's own words" ;; *) fail "lock strict: $out" ;; esac

echo "--- TIER warnings ---"
SHP='shape p\n    name:text\n    age:number = 0\n    born:date\nend shape\nshape s\n    h:p\nend shape\nx = new s\n'
printf '%b' "${SHP}x.h = { name: \"A\", extra: 1 }\nprint x.h.age\n" >"$work/w1.bas"
out=$(cd "$work" && "$G" w1.bas 2>"$work/w1.err")
rc=$?
if [ "$rc" = 0 ] && [ "$out" = 0 ] && grep -q 'p has no field extra, so it was dropped.*\[2126\]' "$work/w1.err" &&
   grep -q 'p.born was not given and has no default, so it is nothing.*\[2127\]' "$work/w1.err" &&
   ! grep -q 'p.age' "$work/w1.err" && [ "$(wc -l <"$work/w1.err")" = 2 ]; then
    pass "an extra field dropped (2126), a missing one with no default (2127), age's default silent; it runs on"
else
    fail "warnings: exit $rc, out [$out], stderr: $(cat "$work/w1.err")"
fi
printf '%b' "${SHP}x.h = { name: \"A\", born: \"2001-01-01\" }\nprint x.h.born\n" >"$work/w2.bas"
out=$(cd "$work" && "$G" w2.bas 2>&1)
[ "$out" = "2001-01-01" ] && pass "nothing tolerated, nothing said" || fail "the clean conversion said: $out"
printf '%b' "on warning stop\n${SHP}x.h = { name: \"A\", extra: 1 }\nprint \"ran on\"\n" >"$work/w3.bas"
out=$(cd "$work" && "$G" w3.bas 2>&1)
case "$out" in *"ran on"*) fail "on warning stop did not refuse: $out" ;; *"p has no field extra"*) pass "on warning stop makes the drop a refusal" ;; *) fail "on warning stop: $out" ;; esac

echo "--- TIER parse ---"
refuse "another language's type" p1.bas 'shape s\n    n:int\nend shape\n' "'int' is not a gBASIC type; write n:number"
refuse "a statement in the body" p2.bas 'shape s\n    n:number\n    print 1\nend shape\n' "a shape's body holds fields"
refuse "an if in the body" p3.bas 'shape s\n    n:number\n    if 1 then\nend shape\n' "a shape's body holds fields"
refuse "an error inside a method keeps its own message" p4.bas 'shape s\n    n:number\n    function m()\n        x = (1\n    end function\nend shape\n' "unexpected \`end\`, expecting \`)\`"
refuse "a default that is not a literal" p5.bas 'shape s\n    n:number = len("ab")\nend shape\n' "n's default is not a literal"
refuse "the wrong end" p6.bas 'shape s\n    n:number\nend shapes\n' 'a shape ends with `end shape`, not `end shapes`'
refuse "a dotted method" p7.bas 'shape s\n    n:number\n    function s.m()\n        return 1\n    end function\nend shape\n' 'a method in a shape is named alone'
runs "literal defaults: signed, [ ], { }, a duration" 'shape s\n    a:number = -2\n    b = [1, "x"]\n    c = { k: [true] }\n    d:duration = 3 days\nend shape\nprint s.a\nprint s.b\nprint s.c\nprint s.d\n' '-2\n[1,"x"]\n{"k":[true]}\n3 days'
ast=$("$G" --ast tests/shapes/instances.bas)
case "$ast" in
    *'Shape "account"'*'Field balance lock(money(USD))'*'Field holder lock(person)'*'Field id lock(number) policy(reset)'*'Function deposit'*)
        pass "--ast shows each field, its lock and policy, and the methods" ;;
    *) fail "--ast: $(printf '%s' "$ast" | grep -A3 'Shape "account"')" ;;
esac
if command -v bison >/dev/null 2>&1; then
    conflicts=$(bison -Wall -o "$work/p.c" src/parser.y 2>&1 | grep -c conflict)
    [ "$conflicts" = 0 ] && pass "zero grammar conflicts" || fail "grammar conflicts: $(bison -Wall -o "$work/p.c" src/parser.y 2>&1 | grep conflict)"
fi

echo "--- TIER hoist ---"
runs "a shape beside program main, used inside it" 'shape s\n    n:number = 1\nend shape\nprogram main(args)\n    x = new s with { n: "5" }\n    print x.n\nend program\n' '5'
printf 'shape s\n    n:number = 1\nend shape\nprogram main(args)\n    print new s\nend program\n' >"$work/h2.bas"
out=$("$G" --check "$work/h2.bas"); [ $? = 0 ] && [ -z "$out" ] && pass "--check: clean" || fail "--check: $out"

echo "--- TIER valgrind ---"
. tests/valgrind_tier.sh
if vg_available; then
    bad=""
    vg_run -q "$G" tests/shapes/instances.bas >/dev/null 2>"$work/vg" || bad="$bad instances.bas"
    vg_run -q "$G" tests/shapes/locks.bas >/dev/null 2>"$work/vg" || bad="$bad locks.bas"
    for f in "$work"/f*.bas "$work"/w*.bas "$work"/p*.bas "$work"/l*.bas; do
        (cd "$work" && vg_run -q "$G" "$(basename "$f")" >/dev/null 2>"$work/vg.$(basename "$f")")
        [ $? = "$VG_EXIT" ] && bad="$bad $(basename "$f")"
    done
    [ -z "$bad" ] && pass "the golden and every refusal: nothing lost, nothing misread" || fail "valgrind:$bad"
else
    printf 'SKIP valgrind (no valgrind)\n'
fi

[ "$status" = 0 ] && echo "PASS tests/run_shapes.sh" || echo "FAIL tests/run_shapes.sh"
exit $status
