#!/usr/bin/env bash
# Vectors (docs/vectors_design.md), increment 1: a packed float32 value,
# its arithmetic and its lock.
#
# TIERS
#   golden     tests/vectors/values.bas against values.out, every line worked
#              out by hand (3-4-5 triangles, unit axes, a float32 that cannot
#              hold 16777217); interpreted and compiled, nothing on stderr.
#   oracle     tests/vectors/oracle.py writes random vectors (dimensions 1 to
#              1536; tiny, huge, whole, zero and negative-zero elements) and
#              judges what gBASIC answers from Python's own arithmetic,
#              EXACTLY: the design fixes the order of every sum, so each
#              answer is one double. Its control counts answers a plain
#              left-to-right sum would get wrong, which must be some.
#   refusals   each refusal by its message, beside the golden's neighbour.
#   serialize  tag 16 pinned, and every corrupt vector payload refused.
#   parse      vector(N) refused at parse time when N is no dimension, and by
#              --check with nothing run; zero grammar conflicts.
#   cost       nearest() against the same ranking as a gBASIC loop, as a
#              ratio in one process (so load slows both); and the memory of
#              2000 vectors of 1536 against the same numbers as arrays.
#   valgrind   the golden and the oracle's run.
. "$(dirname "$0")/portable.sh"
. "$(dirname "$0")/premise.sh"
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null 2>&1 || { echo "FAIL run_vectors: build failed"; exit 1; }
status=0
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
fail() { printf 'FAIL %s\n' "$1"; status=1; }
pass() { printf 'ok   %s\n' "$1"; }

echo "--- TIER golden ---"
for lane in interpreted compiled; do
    rc=0
    if [ "$lane" = compiled ]; then
        GBASIC_COMPILE=all timeout -k 5 120 ./gbasic tests/vectors/values.bas >"$tmp/g.out" 2>"$tmp/g.err" </dev/null || rc=$?
    else
        timeout -k 5 120 ./gbasic tests/vectors/values.bas >"$tmp/g.out" 2>"$tmp/g.err" </dev/null || rc=$?
    fi
    if [ "$rc" != 0 ] || [ -s "$tmp/g.err" ] || ! diff -u tests/vectors/values.out "$tmp/g.out" >"$tmp/g.diff"; then
        head -20 "$tmp/g.diff"; head -3 "$tmp/g.err"
        fail "values.bas $lane (rc $rc)"
    else
        pass "values.bas $lane: the hand-worked golden, nothing on stderr"
    fi
done

echo "--- TIER oracle ---"
if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP oracle (no python3: the arithmetic was held to NOTHING outside gBASIC)\n'
else
    for seed in 1 2 3; do
        python3 -I tests/vectors/oracle.py gen "$seed" "$tmp/cases.txt"
        rc=0
        timeout -k 5 120 ./gbasic tests/vectors/oracle.bas "$tmp/cases.txt" >"$tmp/ans.txt" 2>"$tmp/ans.err" </dev/null || rc=$?
        if [ "$rc" != 0 ] || [ -s "$tmp/ans.err" ]; then
            head -3 "$tmp/ans.err"; fail "oracle seed $seed: gBASIC exited $rc"
            continue
        fi
        if python3 -I tests/vectors/oracle.py check "$tmp/cases.txt" "$tmp/ans.txt" >"$tmp/judge.txt"; then
            pass "oracle seed $seed: $(tail -1 "$tmp/judge.txt")"
        else
            grep MISMATCH "$tmp/judge.txt" | head -8; fail "oracle seed $seed"
        fi
    done
fi

echo "--- TIER refusals ---"
said() {
    printf '%s\n' "$1" >"$tmp/r.bas"
    { timeout -k 5 30 ./gbasic "$tmp/r.bas" 2>&1 </dev/null || true; } | head -1 |
        sed 's/^[a-z]* error at [^:]*:[0-9]*:[0-9]*: //'
}
refuse() {
    local got; got="$(said "$2")"
    case "$got" in
        "$3"*) pass "$1" ;;
        *) printf 'MISMATCH %s\n  want: %s\n  got:  %s\n' "$1" "$3" "$got"; status=1 ;;
    esac
}
refuse 'an empty array' 'v = vector([])' 'vector: an empty array makes no vector'
refuse 'an element not a number, by index' 'v = vector([1, "a"])' 'vector: the element at index 1 is a string, not a number'
refuse 'NaN' 'v = vector([1, decode("nan")])' 'vector: the element at index 1 is nan, and a vector holds finite numbers'
refuse 'past a float32' 'v = vector([1e39])' 'vector: the element at index 0 is 1e+39, and a vector holds finite numbers a float32 can'
refuse 'text that is no list' 'v = vector("{}")' 'vector: this text is a record, not a list of numbers'
refuse 'a number' 'v = vector(3)' 'vector expects an array of numbers, the text of one, or a vector, and this is a number'
refuse 'two dimensions' 'print dot(vector([1, 2]), vector([1]))' 'dot: these vectors have 2 and 1 dimensions'
refuse 'an array where a vector goes' 'print norm([3, 4])' 'norm expects a vector as its first argument, and this is an array; make one with vector(...)'
refuse 'a cosine of zeros' 'print cosine(vector([0, 0]), vector([1, 0]))' 'cosine: the first vector is all zeros, which has no direction'
refuse 'a cosine distance of zeros' 'print distance(vector([1, 0]), vector([0, 0]), "cosine")' 'distance: the second vector is all zeros'
refuse 'normalize zeros' 'print normalize(vector([0]))' 'normalize: this vector is all zeros'
refuse 'a metric not one of three' 'print distance(vector([1]), vector([2]), "manhattan")' 'distance: the metric is "l2", "cosine" or "ip", not "manhattan"'
refuse 'nearest: an element not a vector' 'print nearest(vector([1]), [vector([1]), [2]], 1)' 'nearest: the element at index 1 is an array, not a vector'
refuse 'nearest: a dimension' 'print nearest(vector([1]), [vector([1, 2])], 1)' 'nearest: the element at index 0 has 2 dimensions and the query 1'
refuse 'nearest: a zero row under cosine' 'print nearest(vector([1]), [vector([0])], 1, "cosine")' 'nearest: the element at index 0 is all zeros'
refuse 'nearest: k' 'print nearest(vector([1]), [vector([1])], 1.5)' 'nearest: k is how many to answer, a whole number from 0'
refuse 'nearest: arity' 'print nearest(vector([1]), [])' 'nearest expects a query vector, an array of vectors, how many (k)'
refuse 'bytes not whole float32s' 'print vector_from_bytes("abc")' 'vector_from_bytes: 3 bytes is not a whole number of float32s'
refuse 'bytes of a NaN' 'print vector_from_bytes(hex_decode("0000c07f"))' 'vector_from_bytes: the float32 at index 0 (bytes 0 to 3) is not a number'
refuse 'a vector plus a number' 'print vector([1]) + 1' 'vectors add and subtract with a vector of the same dimension and multiply or divide by a number; this is a vector + a number'
refuse 'a vector times a vector' 'print vector([1]) * vector([1])' 'vectors add and subtract with a vector of the same dimension and multiply or divide by a number; this is a vector * a vector'
refuse 'a sum of two dimensions' 'print vector([1]) + vector([1, 2])' '+: these vectors have 1 and 2 dimensions'
refuse 'divided by zero' 'print vector([1]) / 0' 'division by zero'
refuse 'scaled past a float32' 'print vector([3e38]) * 10' 'the element at index 0 would be beyond a float32'"'"'s range'
refuse 'an order' 'print vector([1]) < vector([2])' 'vectors support only = and !=; compare their distances instead'
refuse 'a write in place' 'v = vector([1])
v[0] = 2' 'a vector is not changed in place; build a new one'
refuse 'an index out of range' 'v = vector([1])
print v[1]' 'vector index out of range'
refuse 'a lock: the dimension' 'x:vector(3) = [1, 2]' 'x is locked to vector by x:vector(3) at line 1, and this value does not convert: it has 2 dimensions, not 3'
refuse 'a lock: what vector() refuses, in its words' 'x:vector = [true]' 'x is locked to vector by x:vector at line 1, and this value does not convert: vector: the element at index 0 is a boolean, not a number'
refuse 'a parameter lock' 'function f(v:vector(2))
    return v
end function
print f([1])' 'v is locked to vector by v:vector(2)'
refuse 'a return type' 'function f():vector(2)
    return [1, 2, 3]
end function
print f()' 'f returns vector, declared by f(...):vector(2) at line 1, and this value does not convert: it has 3 dimensions, not 2'
refuse 'a shape field' 'shape doc
    e:vector(2)
end shape
d = new doc with { e: [1] }' 'e is locked to vector by e:vector(2)'

echo "--- TIER serialize ---"
cat >"$tmp/s.bas" <<'BAS'
function refused(payload)
    on error goto no
    v = deserialize(hex_decode(payload))
    return "read " + v
no:
    return "refused"
end function
print hex_encode(serialize(vector([1.5, -2])))
print refused("674253031002" + "0000c03f" + "000000c0")
print refused("6742530310" + "00")
print refused("6742530310" + "03" + "0000c03f000000c0")
print refused("674253031001" + "0000c07f")
print refused("674253031001" + "0000807f")
print refused("674253021001" + "0000c03f")
print refused("6742530310" + "ffffffffffffffff7f")
BAS
got="$(timeout -k 5 30 ./gbasic "$tmp/s.bas" 2>&1 </dev/null | tr '\n' '|')"
want='674253031002' # pinned: tag 16, dimension 2
want="${want}0000c03f000000c0|read vector[1.5,-2]|refused|refused|refused|refused|refused|refused|"
if [ "$got" = "$want" ]; then
    pass "tag 16 pinned; a dimension 0, one past the bytes, a NaN, an infinity, a v2 header and a 63-bit dimension each refused"
else
    printf '  want: %s\n  got:  %s\n' "$want" "$got"; fail "serialize"
fi

echo "--- TIER parse ---"
for case in 'x:vector(0) = [1]|0' 'x:vector(2.5) = [1]|2.5' 'x:vector(abc) = [1]|abc'; do
    src="${case%|*}"; arg="${case##*|}"
    printf 'print "ran"\n%s\n' "$src" >"$tmp/p.bas"
    out="$({ ./gbasic "$tmp/p.bas" 2>&1 </dev/null || true; })"
    case "$out" in
        "syntax error"*"vector takes its dimension, a whole number from 1, as in vector(1536), not '$arg'") pass "$src refused before anything ran" ;;
        *) printf '  got: %s\n' "$out"; fail "$src" ;;
    esac
done
refuse 'only money and vector take an argument' 'x:number(3) = 1' 'number takes no argument; only money and vector do, as in money(USD) and vector(1536)'
printf 'x:vector(1536) = [1]\nprint "ran"\n' >"$tmp/c1.bas"
printf 'write({file}"%s/ran", "x")\nx:vector(0) = [1]\n' "$tmp" >"$tmp/c2.bas"
c1="$(./gbasic --check "$tmp/c1.bas" 2>&1 || true)"
c2rc=0; c2="$(./gbasic --check "$tmp/c2.bas" 2>/dev/null)" || c2rc=$?
if [ -z "$c1" ] && [ "$c2rc" = 1 ] && [ ! -e "$tmp/ran" ] &&
   printf '%s' "$c2" | grep -q 'vector takes its dimension'; then
    pass "--check: vector(1536) clean, vector(0) refused with exit 1, nothing run"
else
    printf '  c1: %s\n  c2 (rc %s): %s\n' "$c1" "$c2rc" "$c2"; fail "--check"
fi
if command -v bison >/dev/null 2>&1; then
    conf="$({ bison -d -o "$tmp/p.c" src/parser.y 2>&1 || true; } | grep -ci conflict || true)"
    if [ "${conf:-0}" -eq 0 ]; then
        pass "bison reports zero conflicts with vector(N)"
    else
        fail "the grammar has conflicts"
    fi
else
    printf 'SKIP grammar (no bison: the conflict count was NOT measured)\n'
fi

echo "--- TIER cost ---"
# The ranking nearest() does, written as the gBASIC loop it replaces, over
# the same 1000 vectors of 1536: a ratio in one process, so a loaded machine
# slows both. Measured 2026-11-27: about 400x; 50x is the floor.
cat >"$tmp/cost.bas" <<'BAS'
function loop_cosine(a, b)
    d = 0
    na = 0
    nb = 0
    for i = 0 to len(a) - 1
        d = d + a[i] * b[i]
        na = na + a[i] * a[i]
        nb = nb + b[i] * b[i]
    next
    return 1 - d / (sqrt(na) * sqrt(nb))
end function
base = []
for i = 0 to 1535
    base = append(base, random() - 0.5)
next
q = vector(base)
rows = []
plain = []
for r = 0 to 999
    v = q * (1 + r / 1000) + vector(array(q)) * random()
    rows = append(rows, v)
    plain = append(plain, array(v))
next
t0 = monotonic()
for rep = 1 to 20
    best = nearest(q, rows, 5, "cosine")
next
fast = (monotonic() - t0) / 20
t0 = monotonic()
qa = array(q)
best_d = 9
for each p in plain
    d = loop_cosine(qa, p)
    if d < best_d then
        best_d = d
    end if
next
slow = monotonic() - t0
print "ratio " + round(slow / fast)
BAS
ratio="$(timeout -k 5 300 ./gbasic "$tmp/cost.bas" 2>&1 </dev/null | sed -n 's/^ratio //p')"
if [ -n "$ratio" ] && [ "$ratio" -ge 50 ]; then
    pass "nearest() ranks ${ratio}x faster than the same ranking as a gBASIC loop (floor 50x)"
else
    fail "nearest() is only ${ratio:-?}x the gBASIC loop (floor 50x)"
fi
if command -v python3 >/dev/null 2>&1; then
    for form in vector array; do
        cat >"$tmp/mem_$form.bas" <<BAS
base = []
for i = 0 to 1535
    base = append(base, i / 1536)
next
rows = []
for r = 0 to 1999
    rows = append(rows, $( [ "$form" = vector ] && echo 'vector(base)' || echo 'array(vector(base))' ))
    base[0] = r
next
print len(rows)
BAS
    done
    printf 'print 1\n' >"$tmp/mem_none.bas"
    peak() {
        python3 -I -c 'import resource, subprocess, sys
subprocess.run(sys.argv[1:], stdout=subprocess.DEVNULL, check=True)
print(resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss)' ./gbasic "$1"
    }
    none="$(peak "$tmp/mem_none.bas")"; vec="$(peak "$tmp/mem_vector.bas")"; arr="$(peak "$tmp/mem_array.bas")"
    vk=$((vec - none)); ak=$((arr - none))
    if [ "$vk" -gt 0 ] && [ $((vk * 5)) -le "$ak" ]; then
        pass "2000 vectors of 1536 add ${vk} KB, the same numbers as arrays ${ak} KB (at most a fifth)"
    else
        fail "2000 vectors of 1536 add ${vk} KB against ${ak} KB as arrays (wanted at most a fifth)"
    fi
else
    printf 'SKIP memory (no python3 to read a child'"'"'s peak)\n'
fi

echo "--- TIER valgrind ---"
. "$(dirname "$0")/valgrind_tier.sh"
if vg_available; then
    for f in values oracle; do
        rc=0
        if [ "$f" = oracle ]; then
            [ -f "$tmp/cases.txt" ] || printf 'pair p0 0000803f 00000040 2\n' >"$tmp/cases.txt"
            timeout -k 5 600 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic tests/vectors/oracle.bas "$1"' _ "$tmp/cases.txt" \
                >/dev/null 2>"$tmp/vg.err" </dev/null || rc=$?
        else
            timeout -k 5 600 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic tests/vectors/values.bas' \
                >/dev/null 2>"$tmp/vg.err" </dev/null || rc=$?
        fi
        if [ "$rc" = "$VG_EXIT" ] || [ "$rc" = 124 ]; then
            tail -30 "$tmp/vg.err"; fail "valgrind $f (rc $rc)"
        else
            pass "valgrind $f: no definite leak or invalid access"
        fi
    done
else
    printf 'SKIP valgrind (unavailable)\n'
fi

if [ "$status" = 0 ]; then
    echo "PASS tests/run_vectors.sh"
else
    echo "FAIL tests/run_vectors.sh"
fi
exit $status
