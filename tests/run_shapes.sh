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
#   csv       increment 3 (§6.1): tests/shapes/csv.bas against csv.out (worked
#             by hand, both lanes): the header matched once by name, then with
#             case, spaces and _ ignored; each cell converted into its field; an
#             empty cell nothing outside text; names:, options after the shape,
#             problems: "deliver"; csv.write's header from the shape even for no
#             rows, each row converted first; and a round trip read back equal.
#             Then the warnings, said once at open however many records follow,
#             and the refusals, each beside its working neighbour: a cell naming
#             its line, record and column (error.details too), two columns for a
#             field, a column for two fields, a column for a shaped field, a
#             shape with header: false and no names, `on warning stop`.
#   schema    json_schema (§6.2): a schema written out by hand, keys in the order
#             tools.param_schema writes; refusals (a plain record, a shape inside
#             itself). Then WHAT THE SCHEMA ADMITS, THE LOCK CONVERTS:
#             tests/shapes/schema_oracle.py writes 3000 documents for
#             schema_check.bas's shape of every kind, near misses among them,
#             judges each against the schema gBASIC printed (JSON Schema and RFC
#             3339 read from their specs, not from gBASIC), and every one it
#             admits must convert under `on warning stop`, at least 1000 of them.
#             The control reads the same schema with its patterns ignored and
#             must find admitted documents the locks refuse: RFC 3339 allows
#             fractional seconds and a lowercase t, and its time an offset.
#             Skips without python3.
#   gsql      §6.3: tests/shapes/gsql.bas against gsql.out (worked by hand, both
#             lanes): rows read into a shape (a query and a path, the element an
#             instance with its methods), a shape in a record column, and
#             gsql.expect naming every problem at once, in error.details too.
#             Then a file field beside a column of its name, a table that is not
#             there, a plain record for a shape, and a handle's method beside
#             the module's. Skips when the build has no G-SQL.
#   sites     increment 4 (§7): a `.field` site keeps the slot it last found
#             its field at, and a write site the shape field it converted by,
#             each checked by name on every use. tests/shapes/sites.bas (both
#             lanes, self-checking) has ONE site meet records of every layout:
#             plain in two orders, two shapes holding the field at different
#             positions (number in one, text in the other), a record of 21
#             fields (hashed), one without the field, a shared record written.
#   frames    increment 5 (§6.4): tests/shapes/frames.bas against frames.out
#             (worked by hand, both lanes): frame.read_csv(path, shape) through
#             csv.records(f, shape), the columns the fields in declared order;
#             frame.to_rows(df, shape) the same invoices csv.records reads, and
#             back by from_rows; frame.conform(df, shape) for a frame from
#             elsewhere; the missing-cell verbs seeing a shaped frame's nothing
#             (they saw only unknown: drop_missing kept the rows, coerce wrote
#             "nothing"); and convert(record, shape), the lock when the shape is
#             a value. Then the columns judged once: a dropped column and a
#             field with none said once each for 50 rows, and for no rows;
#             `on warning stop` refusing there in the warning's words; and the
#             refusals, each beside its neighbour: a cell by its row (from 1,
#             as csv.write counts), read_csv's by line, a plain record or a
#             number for a shape, convert's own.
#   cost      the slot found once a site, counted by callgrind: reading, and
#             writing, the 8th of 8 fields costs what the 1st does, within 2%
#             of the whole run (before: 8% more to read, 6% to write). Skips
#             without valgrind.
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

if "$G" tests/shapes/csv.bas "$work" 2>"$work/err" | cmp -s - tests/shapes/csv.out && [ ! -s "$work/err" ]; then
    pass "csv.bas prints the hand-worked golden, nothing on stderr"
else
    fail "csv.bas: $(diff <("$G" tests/shapes/csv.bas "$work" 2>&1) tests/shapes/csv.out | head -5)"
fi
if GBASIC_COMPILE=all "$G" tests/shapes/csv.bas "$work" 2>/dev/null | cmp -s - tests/shapes/csv.out; then
    pass "and compiled, the same bytes"
else
    fail "csv.bas compiled differs"
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

echo "--- TIER csv ---"
CH='shape inv\n    day:date\n    amount:money(USD)\n    who:text = "?"\nend shape\n'
printf '%b' "${CH}for each r in csv.parse(\"day,amount,Notes\\\\n2026-01-01,1,x\\\\n2026-01-02,2,y\\\\n2026-01-03,3,z\\\\n\", inv)\n    print r.amount\nend for\nfor each r in csv.parse(\"Day\\\\n2026-01-01\\\\n2026-01-02\\\\n\", inv)\n    print r.amount\nend for\n" >"$work/cw.bas"
out=$(cd "$work" && "$G" cw.bas 2>"$work/cw.err"); rc=$?
if [ $rc = 0 ] && [ "$out" = "$(printf '1.00\n2.00\n3.00\nnothing\nnothing')" ] &&
   grep -q "csv.parse: column 'Notes' of the header on line 1 matches no field of inv, so it was dropped (inv is declared at line 1) at .*cw.bas:6:1 \[2126\]" "$work/cw.err" &&
   grep -q "csv.parse: no column of the header on line 1 matches inv.amount, which has no default, so it is nothing (inv is declared at line 1) at .*cw.bas:9:1 \[2127\]" "$work/cw.err" &&
   ! grep -q 'inv.who\|inv.day' "$work/cw.err" && [ "$(wc -l <"$work/cw.err")" = 2 ]; then
    pass "a column no field matches, and a field no column matches, each said once at open for three records; a default silent"
else
    fail "csv warnings: exit $rc, out [$out], stderr: $(cat "$work/cw.err")"
fi
runs "the working neighbour: every column a field" "${CH}for each r in csv.parse(\"day,AMOUNT\\\\n2026-01-01,4\\\\n\", inv)\n    print r.amount\nend for\n" '4.00'
refuse "a cell that does not convert, by line, record and column" c1.bas "${CH}for each r in csv.parse(\"day,amount\\\\n2026-01-01,1\\\\n2026-01-02,lots\\\\n\", inv)\n    print r.amount\nend for\n" "c1.bas:6:1: csv.parse: line 3 (record 2), column 'amount': amount is locked to money by amount:money(USD) at line 3, and this value does not convert"
runs "and its details: line, offset, record" "${CH}on error goto next\nfor each r in csv.parse(\"day,amount\\\\n2026-01-01,lots\\\\n\", inv)\nend for\ne = error\nprint e.details\n" '{"line":2,"offset":11,"record":1}'
refuse "two columns for one field" c2.bas "${CH}for each r in csv.parse(\"Day,DAY,amount\\\\n2026-01-01,1,2\\\\n\", inv)\nend for\n" "csv.parse: columns 'Day' and 'DAY' of the header on line 1 both match inv.day"
runs "the working neighbour: an exact name beside a near one takes the exact, the other dropped" "${CH}on warning ignore\nfor each r in csv.parse(\"Day,day,amount\\\\n2026-01-01,2026-02-02,2\\\\n\", inv)\n    print r.day\nend for\n" '2026-02-02'
refuse "a column for two fields" c3.bas 'shape ab\n    a_b:number = 0\n    ab_:number = 0\nend shape\nfor each r in csv.parse("A B\\n1\\n", ab)\nend for\n' "column 'A B' of the header on line 1 matches both ab.a_b and ab.ab_"
refuse "a column for a shaped field" c4.bas "${CH}shape h\n    i:inv\nend shape\nfor each r in csv.parse(\"i\\\\n1\\\\n\", h)\nend for\n" "column 'i' of the header on line 1 matches h.i, which is an inv, and one field of a file cannot hold a record"
refuse "a shape with header: false and no names" c5.bas "${CH}for each r in csv.parse(\"1,2\\\\n\", inv, { header: false })\nend for\n" 'a shape is matched to the columns by name, and with header: false they have none; give them with names:'
refuse "three arguments, the second no shape" c6.bas "${CH}for each r in csv.parse(\"a\\\\n1\\\\n\", { x: 1 }, {})\nend for\n" 'csv.parse: the second of three arguments is a shape'
printf '%b' "on warning stop\n${CH}for each r in csv.parse(\"day,amount,z\\\\n2026-01-01,1,2\\\\n\", inv)\n    print \"ran\"\nend for\n" >"$work/c7.bas"
out=$(cd "$work" && "$G" c7.bas 2>&1)
case "$out" in *ran*) fail "on warning stop let a column drop: $out" ;; *"column 'z' of the header on line 1 matches no field of inv, so it was dropped"*) pass "on warning stop: a dropped column refused at open, before a record, in the warning's words" ;; *) fail "csv strict: $out" ;; esac
refuse "csv.write: a row that is not a record" c8.bas "${CH}csv.write(\"o.csv\", [[1]], inv)\n" 'csv.write: row 1 is an array, and only a record converts into shape inv'
refuse "csv.write: a row that does not convert, by its number" c9.bas "${CH}csv.write(\"o.csv\", [{ day: \"2026-01-01\" }, { day: \"soon\" }], inv)\n" 'csv.write: row 2: day is locked to date by day:date at line 2'
[ ! -e "$work/o.csv" ] && pass "and it wrote nothing" || fail "csv.write refused but wrote o.csv"
refuse "csv.write: names: beside a shape" c10.bas "${CH}csv.write(\"o.csv\", [], inv, { names: [\"a\"] })\n" 'csv.write: a shape gives the header, so names: cannot go with one'

echo "--- TIER schema ---"
runs "a schema by hand: types, a format and pattern, required where no default" 'shape p\n    name:text\n    age:number = 0\n    born:date\n    note = nothing\nend shape\nprint json_encode(json_schema(p))\n' '{"type":"object","properties":{"name":{"type":"string"},"age":{"type":"number"},"born":{"type":"string","format":"date","pattern":"^[0-9]{4}-(0[1-9]|1[0-2])-(0[1-9]|[12][0-9]|3[01])$"},"note":{}},"required":["name","born"],"additionalProperties":false}'
runs "a shaped field is its shape's object; money a decimal string of the digits USD stores" 'shape q\n    amt:money(USD)\nend shape\nshape p\n    q:q\n    ok:boolean = true\nend shape\nprint json_encode(json_schema(p))\n' '{"type":"object","properties":{"q":{"type":"object","properties":{"amt":{"type":"string","pattern":"^-?[0-9]{1,12}(\\\\.[0-9]{1,6})?$"}},"required":["amt"],"additionalProperties":false},"ok":{"type":"boolean"}},"required":["q"],"additionalProperties":false}'
printf 'load tools\nshape p\n    n:number\nend shape\nprint join(keys(json_schema(p)), ",")\nprint join(keys(tools.param_schema({ params: [{ name: "n", type: "number", describe: "", required: true }] })), ",")\nprint json_schema(new p with { n: 1 }) = json_schema(p)\n' >"$work/s0.bas"
out=$(cd "$work" && GBASIC_PATH="$OLDPWD/stdlib" "$G" s0.bas 2>&1)
[ "$out" = "$(printf 'type,properties,required,additionalProperties\ntype,properties,required\ntrue')" ] && pass "its keys begin as tools.param_schema's do; an instance describes its shape" || fail "schema keys: $out"
refuse "a plain record has no declared form" s1.bas 'print json_schema({ a: 1 })\n' 'json_schema expects a shape, such as invoice from `shape invoice`; a plain record has no declared form to describe'
refuse "a shape inside itself" s2.bas 'shape node\n    v:number = 0\n    next:node = nothing\nend shape\nprint json_schema(node)\n' 'json_schema: node.next is a node, a shape this schema is already describing, so written out in full it would never end (node is declared at line 1)'
B='{"type":"object","properties":{"x":{"type":"object","properties":{"n":{"type":"number"}},"required":[],"additionalProperties":false}},"required":["x"],"additionalProperties":false}'
runs "the working neighbour: one shape held twice, not inside itself, is written twice" 'shape a\n    n:number = 0\nend shape\nshape b\n    x:a\nend shape\nshape c\n    y:b\n    z:b\nend shape\nprint json_encode(json_schema(c))\n' "{\"type\":\"object\",\"properties\":{\"y\":$B,\"z\":$B},\"required\":[\"y\",\"z\"],\"additionalProperties\":false}"
refuse "a value that is no record" s3.bas 'print json_schema(5)\n' 'json_schema expects a shape'
if command -v python3 >/dev/null 2>&1; then
    "$G" tests/shapes/schema_check.bas schema >"$work/schema.json" &&
        python3 tests/shapes/schema_oracle.py generate "$work/schema.json" "$work/docs.jsonl" &&
        "$G" tests/shapes/schema_check.bas convert "$work/docs.jsonl" >"$work/verdicts.txt"
    if out=$(python3 tests/shapes/schema_oracle.py judge "$work/schema.json" "$work/docs.jsonl" "$work/verdicts.txt"); then
        pass "every document the schema admits converts: $(printf '%s' "$out" | head -1)"
    else
        fail "the schema admits what the locks refuse: $out"
    fi
    if out=$(python3 tests/shapes/schema_oracle.py judge "$work/schema.json" "$work/docs.jsonl" "$work/verdicts.txt" --formats-only); then
        pass "control: formats without the patterns admit what the locks refuse: $(printf '%s' "$out" | grep '^documents')"
    else
        fail "control: without its patterns the schema still admitted nothing the locks refuse, so the tier proves nothing: $out"
    fi
else
    printf 'SKIP schema oracle (no python3): nothing measured about what the schema admits\n'
fi

echo "--- TIER gsql ---"
printf 'load gsql\nprint "loaded"\n' >"$work/gprobe.bas"
if [ "$("$G" "$work/gprobe.bas" 2>&1)" = loaded ]; then
    mkdir -p "$work/g1" "$work/g2"
    if GSQL_DIR="$work/g1" timeout -k 5 60 "$G" tests/shapes/gsql.bas 2>"$work/err" | cmp -s - tests/shapes/gsql.out && [ ! -s "$work/err" ]; then
        pass "gsql.bas prints the hand-worked golden, nothing on stderr"
    else
        fail "gsql.bas: $(diff <(GSQL_DIR="$work/g1b" timeout -k 5 60 "$G" tests/shapes/gsql.bas 2>&1) tests/shapes/gsql.out | head -5)"
    fi
    if GSQL_DIR="$work/g2" GBASIC_COMPILE=all timeout -k 5 60 "$G" tests/shapes/gsql.bas 2>/dev/null | cmp -s - tests/shapes/gsql.out; then
        pass "and compiled, the same bytes"
    else
        fail "gsql.bas compiled differs"
    fi
    GQ="load gsql\ndb = gsql.connect(\"$work/g3\", { create: true, linger: 0 })\ndb.exec(\"drop table if exists t\")\ndb.exec(\"create table t (n number, spot text)\")\n"
    refuse "a file field beside a column of its name" g1.bas "${GQ}shape s\n    n:number\n    spot:file\nend shape\ngsql.expect(db, \"t\", s)\n" 'gsql.expect: table t does not hold shape s (s is declared at line 5): s.spot is a file, which no column holds'
    runs "the working neighbour: the same table and a shape it holds" "${GQ}shape s\n    n:number\n    spot:text\nend shape\ngsql.expect(db, \"t\", s)\nprint \"held\"\n" 'held'
    refuse "a text field beside a number column, and a number beside text" g4.bas "${GQ}shape s\n    n:text\n    spot:number\nend shape\ngsql.expect(db, \"t\", s)\n" 's.n is text, and column n is number; s.spot is number, and column spot is text'
    refuse "a table that is not there" g2.bas "${GQ}shape s\n    n:number\nend shape\ngsql.expect(db, \"nope\", s)\n" 'gsql.expect: there is no table `nope`'
    refuse "a plain record for a shape" g3.bas "${GQ}gsql.expect(db, \"t\", { n: 1 })\n" 'gsql.expect: the third argument is a shape, such as customer from `shape customer`'
else
    printf 'SKIP gsql (this build has no G-SQL): rows into a shape and gsql.expect were NOT measured\n'
fi

echo "--- TIER sites ---"
if "$G" tests/shapes/sites.bas 2>"$work/err" | cmp -s - tests/shapes/sites.out && [ ! -s "$work/err" ]; then
    pass "sites.bas: one site over every layout, each field the right one"
else
    fail "sites.bas: $(diff <("$G" tests/shapes/sites.bas 2>&1) tests/shapes/sites.out | head -5)"
fi
if GBASIC_COMPILE=all "$G" tests/shapes/sites.bas 2>/dev/null | cmp -s - tests/shapes/sites.out; then
    pass "and compiled, the same bytes"
else
    fail "sites.bas compiled differs"
fi
# a field read in place is read only from THIS frame: one from an enclosing
# frame still notes the read, so shadowing the name afterwards warns
printf 'cfg = { level: 3 }\nfunction f()\n    x = cfg.level\n    cfg = { level: 9 }\n    return x\nend function\nprint f()\n' >"$work/shadow.bas"
got=$("$G" "$work/shadow.bas" 2>&1)
case "$got" in
    *"'cfg' was read from an enclosing scope"*"[2104]"*3) pass "a field read from an enclosing frame still warns 2104 when the name is then shadowed" ;;
    *) fail "shadowed after a field read: $got" ;;
esac

echo "--- TIER frames ---"
mkdir -p "$work/fd"
if "$G" tests/shapes/frames.bas "$work/fd" 2>"$work/err" | cmp -s - tests/shapes/frames.out && [ ! -s "$work/err" ]; then
    pass "frames.bas prints the hand-worked golden, nothing on stderr"
else
    fail "frames.bas: $(diff <("$G" tests/shapes/frames.bas "$work/fd" 2>&1) tests/shapes/frames.out | head -5)"
fi
if GBASIC_COMPILE=all "$G" tests/shapes/frames.bas "$work/fd" 2>/dev/null | cmp -s - tests/shapes/frames.out; then
    pass "and compiled, the same bytes"
else
    fail "frames.bas compiled differs"
fi
FH='load frame\nshape inv\n    id:text\n    amount:money(USD)\n    paid:boolean = false\nend shape\n'
# 50 rows with a column the shape lacks and no column for id: each said once,
# at the columns, before the rows; and with no rows at all, the same two (a
# program of its own: one site says one message once a run)
fwarn() {   # fwarn <label> <file> <source> <stdout>
    printf '%b' "$3" >"$work/$2"
    local out rc
    out=$(cd "$work" && "$G" "$2" 2>"$work/$2.err"); rc=$?
    if [ $rc = 0 ] && [ "$out" = "$(printf '%b' "$4")" ] && [ "$(wc -l <"$work/$2.err")" = 2 ] &&
       grep -q 'inv has no field note, so it was dropped converting a record into one (inv is declared at line 2) .*\[2126\]' "$work/$2.err" &&
       grep -q 'inv.id was not given and has no default, so it is nothing (inv is declared at line 2) .*\[2127\]' "$work/$2.err"; then
        pass "$1"
    else
        fail "$1: exit $rc, out [$out], stderr: $(cat "$work/$2.err")"
    fi
}
fwarn "a dropped column and a field with none: said once each for 50 rows" fw.bas "${FH}df = { amount: [], note: [] }\nfor i = 1 to 50\n    append(df.amount, string(i))\n    append(df.note, \"x\")\nend for\nrows = frame.to_rows(df, inv)\nprint len(rows)\nprint rows[49].amount\n" '50\n50.00'
fwarn "and for a frame with no rows" fz.bas "${FH}print frame.conform({ amount: [], note: [] }, inv)\n" '{"id":[],"amount":[],"paid":[]}'
runs "the working neighbour: every field a column, nothing said" "${FH}print frame.to_rows({ id: [\"a\"], amount: [\"1\"] }, inv)\n" '[{"id":"a","amount":1.00,"paid":false}]'
printf '%b' "on warning stop\n${FH}rows = frame.to_rows({ id: [\"a\"], amount: [\"1\"], note: [\"x\"] }, inv)\nprint \"ran\"\n" >"$work/fs.bas"
out=$(cd "$work" && "$G" fs.bas 2>&1)
case "$out" in
    *ran*|*"row 1"*) fail "on warning stop let a column drop, or named a row: $out" ;;
    *"inv has no field note, so it was dropped"*) pass "on warning stop: refused at the columns, in the warning's own words" ;;
    *) fail "frame strict: $out" ;;
esac
refuse "a cell that does not convert, by its row" fr1.bas "${FH}rows = frame.to_rows({ id: [\"a\", \"b\"], amount: [\"1\", \"lots\"] }, inv)\n" "frame.to_rows: row 2: amount is locked to money by amount:money(USD) at line 4, and this value does not convert"
refuse "and through conform" fr2.bas "${FH}df = frame.conform({ id: [\"a\"], amount: [\"lots\"] }, inv)\n" "frame.conform: row 1: amount is locked to money"
refuse "read_csv: a cell by line and column, csv.records' own words" fr3.bas "${FH}write({file}\"in.csv\", \"id,amount\\\\na,1\\\\nb,lots\\\\n\")\ndf = frame.read_csv(\"in.csv\", inv)\n" "csv.records: line 3 (record 2), column 'amount': amount is locked to money"
printf '%b' "${FH}write({file}\"in.csv\", \"ID,Amount,extra\\\\na,1,x\\\\nb,2,y\\\\n\")\ndf = frame.read_csv(\"in.csv\", inv)\nprint df\n" >"$work/fc.bas"
out=$(cd "$work" && "$G" fc.bas 2>"$work/fc.err")
[ "$out" = '{"id":["a","b"],"amount":[1.00,2.00],"paid":[false,false]}' ] &&
   [ "$(grep -c "csv.records: column 'extra' of the header on line 1 matches no field of inv, so it was dropped" "$work/fc.err")" = 1 ] &&
   [ "$(wc -l <"$work/fc.err")" = 1 ] &&
    pass "read_csv: the header matched loosely, the dropped column said once" ||
    fail "read_csv shaped: out [$out], stderr: $(cat "$work/fc.err")"
refuse "read_csv: a plain record is no shape" fr4.bas "${FH}df = frame.read_csv(\"in.csv\", { id: 1 })\n" 'frame.read_csv expects a shape, such as invoice from `shape invoice`'
refuse "to_rows: a plain record is no shape" fr5.bas "${FH}rows = frame.to_rows({ id: [\"a\"] }, { id: 1 })\n" 'frame.to_rows expects a shape'
refuse "conform: a number is no shape" fr6.bas "${FH}df = frame.conform({ id: [\"a\"] }, 3)\n" 'frame.conform expects a shape'
refuse "convert: a value that is not a record" fr7.bas "${FH}x = convert([1], inv)\n" 'convert: an array is not a record, and only a record converts into shape inv'
refuse "convert: a plain record is no shape" fr8.bas "${FH}x = convert({ id: \"a\" }, { id: \"b\" })\n" 'convert expects a shape as its second argument'
refuse "convert: one argument" fr9.bas "${FH}x = convert({ id: \"a\" })\n" 'convert expects two arguments: a record and a shape'
runs "the working neighbour: convert of a record" "${FH}print convert({ id: \"a\", amount: \"1.5\" }, inv)\n" '{"id":"a","amount":1.50,"paid":false}'

echo "--- TIER cost ---"
. tests/valgrind_tier.sh
if vg_available && command -v callgrind_annotate >/dev/null 2>&1; then
    # ONE body, the field substituted: what differs between two runs is
    # where the field is, and nothing else
    for op in read write; do
        for f in a h; do
            if [ $op = read ]; then body="s = s + r.$f"; else body="r.$f = r.$f + 1"; fi
            printf 'function run(n)\n    r = new acct\n    s = 0\n    for i = 1 to n\n        %s\n    end for\n    return s\nend function\n\nprogram main(args)\n    print run(200000)\nend program\n\nshape acct\n    a:number = 1\n    b:number = 2\n    c:number = 3\n    d:number = 4\n    e:number = 5\n    f:number = 6\n    g:number = 7\n    h:number = 8\nend shape\n' "$body" >"$work/cost_$op$f.bas"
            vg_callgrind_to "$work/cg.$op$f" "$G" "$work/cost_$op$f.bas" >/dev/null 2>&1
            eval "ir_$op$f=$(callgrind_annotate "$work/cg.$op$f" 2>/dev/null | awk '/PROGRAM TOTALS/ { gsub(",", "", $1); print $1 }')"
        done
        first=$(eval echo "\$ir_${op}a"); last=$(eval echo "\$ir_${op}h")
        if [ -n "$first" ] && [ -n "$last" ] && awk -v a="$first" -v b="$last" 'BEGIN { d = (b - a) / a; exit !(d < 0.02 && d > -0.02) }'; then
            pass "$op the 8th field: $last instructions, the 1st $first, within 2%"
        else
            fail "$op the 8th field: $last instructions against the 1st's $first (more than 2% apart: the slot is searched for again)"
        fi
    done
else
    printf 'SKIP cost (needs valgrind and callgrind_annotate): the slot found once a site was NOT counted\n'
fi

echo "--- TIER valgrind ---"
. tests/valgrind_tier.sh
if vg_available; then
    bad=""
    vg_run -q "$G" tests/shapes/instances.bas >/dev/null 2>"$work/vg" || bad="$bad instances.bas"
    vg_run -q "$G" tests/shapes/locks.bas >/dev/null 2>"$work/vg" || bad="$bad locks.bas"
    vg_run -q "$G" tests/shapes/sites.bas >/dev/null 2>"$work/vg" || bad="$bad sites.bas"
    vg_run -q "$G" tests/shapes/csv.bas "$work" >/dev/null 2>"$work/vg" || bad="$bad csv.bas"
    vg_run -q "$G" tests/shapes/frames.bas "$work/fd" >/dev/null 2>"$work/vg" || bad="$bad frames.bas"
    vg_run -q "$G" tests/shapes/schema_check.bas schema >/dev/null 2>"$work/vg" || bad="$bad schema_check.bas"
    if [ "$("$G" "$work/gprobe.bas" 2>&1)" = loaded ]; then
        mkdir -p "$work/gv"
        GSQL_DIR="$work/gv" vg_run -q "$G" tests/shapes/gsql.bas >/dev/null 2>"$work/vg" || bad="$bad gsql.bas"
    fi
    for f in "$work"/f*.bas "$work"/w*.bas "$work"/p*.bas "$work"/l*.bas "$work"/c*.bas "$work"/s[1-9]*.bas "$work"/g[1-9]*.bas; do
        (cd "$work" && vg_run -q "$G" "$(basename "$f")" >/dev/null 2>"$work/vg.$(basename "$f")")
        [ $? = "$VG_EXIT" ] && bad="$bad $(basename "$f")"
    done
    [ -z "$bad" ] && pass "the golden and every refusal: nothing lost, nothing misread" || fail "valgrind:$bad"
else
    printf 'SKIP valgrind (no valgrind)\n'
fi

[ "$status" = 0 ] && echo "PASS tests/run_shapes.sh" || echo "FAIL tests/run_shapes.sh"
exit $status
