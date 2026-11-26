#!/usr/bin/env bash
# THE FORMS 1.0 KEEPS FREE (docs/shapes_design.md §3 and §9, 2026-11-26).
#
# After 1.0 the core grammar is frozen and backward compatible: new syntax may
# only take forms that are syntax errors in 1.0, so no 1.0 program changes
# meaning. Two pieces of planned syntax are mapped now: shapes (before 1.0)
# and contracts (after it). This suite holds both halves of that promise:
#
#   free     each form a later feature will give meaning to is a syntax error
#            today -- so nothing before it takes the form by accident. When a
#            feature ships, it moves its rows to `taken` deliberately.
#   names    each word stays an ordinary name everywhere else (`shape` is
#            frame.shape and a stats parameter; `requires (x)` is a call).
#   taken    the forms a shipped feature has taken, each now parsing: the
#            rows moved out of `free` on purpose, the day the feature shipped.
#
# Measured with `gbasic --check` (parse and load checks, nothing run).
. "$(dirname "$0")/portable.sh"
set -u
cd "$(dirname "$0")/.."
make >/dev/null 2>&1 || { echo "FAIL run_reserved_forms: build failed"; exit 1; }

status=0
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

free() {   # free <label> <source>: must be a syntax error
    printf '%b' "$2" >"$work/f.bas"
    local out
    out=$(./gbasic --check "$work/f.bas" 2>&1)
    case "$out" in
        *'"code":"GB_DIAG_PARSE_ERROR"'*|*'"code":"GB_DIAG_LEX_'*) printf 'ok   free  %s\n' "$1" ;;
        *) printf 'FAIL free  %s: no longer a syntax error: %s\n' "$1" "$out"; status=1 ;;
    esac
}
taken() {   # taken <label> <source>: a form a shipped feature gave meaning to
    printf '%b' "$2" >"$work/t.bas"
    local out
    out=$(./gbasic --check "$work/t.bas" 2>&1)
    if [ $? = 0 ] && [ -z "$out" ]; then
        printf 'ok   taken %s\n' "$1"
    else
        printf 'FAIL taken %s: %s\n' "$1" "$out"; status=1
    fi
}
name() {   # name <label> <source>: must check clean
    printf '%b' "$2" >"$work/n.bas"
    local out
    out=$(GBASIC_PATH=stdlib ./gbasic --check "$work/n.bas" 2>&1)
    if [ $? = 0 ] && [ -z "$out" ]; then
        printf 'ok   name  %s\n' "$1"
    else
        printf 'FAIL name  %s: %s\n' "$1" "$out"; status=1
    fi
}

echo "--- shapes (docs/shapes_design.md §3) ---"
taken "shape NAME opens a block (increment 1, 2026-11-26)" 'shape account\n    owner:text = "x"\nend shape\n'
free "shape NAME with no end shape"      'shape account\n'
free "an invariant line in a shape (§9)" 'shape account\n    balance:number = 0\n    invariant balance >= 0\nend shape\n'
free "a requires line in a shape"        'shape account\n    balance:number = 0\n    requires balance >= 0\nend shape\n'
free "a lock naming a shape"             'a:account = { owner: "x" }\n'
free "a parameter locked to a shape"     'function post(e:account)\n    return e\nend function\n'
free "a for each element locked to one"  'for each r:invoice in []\nend for\n'
free "shape NAME from NAME"              'shape savings from account\nend shape\n'
name "shape as a variable"               'shape = 3\nprint shape\n'
name "shape as a function and a call"    'function shape(x)\n    return x\nend function\nprint shape(2)\n'
name "shape as a parameter"              'function g(shape, rate)\n    return shape + rate\nend function\nprint g(1, 2)\n'
name "frame.shape"                       'load frame\ndf = frame.from_rows([{ a: 1 }])\nprint frame.shape(df)\n'
name "shape (x), a call"                 'function shape(x)\n    return x\nend function\nshape (1)\n'
name "a field named shape before then"   'fam = { shape: 1 }\nif 1 != fam.shape then\n    print 1\nend if\n'
name "shape NAME inside an expression"   'shape = 1\naccount = 2\nx = [shape, account]\n'

echo "--- contracts (docs/shapes_design.md §9) ---"
free "requires under a function header"  'function f(n)\n    requires n >= 1\n    return n\nend function\n'
free "requires not (...)"                'function f(a, b)\n    requires not (a and b)\n    return a\nend function\n'
free "ensures under a function header"   'function f(n)\n    ensures result >= 0\n    return n\nend function\n'
free "invariant under a while"           'i = 0\nwhile i < 3\n    invariant i <= 3\n    i += 1\nend while\n'
free "invariant under a for"             'for i = 1 to 3\n    invariant i <= 3\nend for\n'
free "property block at the top level"   'property "commutes"\n    print 1\nend property\n'
name "requires as a variable"            'requires = 1\nprint requires\n'
name "ensures as a variable"             'ensures = 1\nprint ensures\n'
name "invariant as a variable"           'invariant = 2\nprint invariant\n'
name "property as a variable"            'property = 3\nprint property\n'
name "requires (x), a call"              'function requires(x)\n    return x\nend function\nfunction f(n)\n    requires (n >= 1)\n    return n\nend function\nprint f(2)\n'
name "a record field named each word"    'r = { shape: 1, requires: 2, ensures: 3, invariant: 4, property: 5 }\nprint r.shape + r.property\n'

[ "$status" = 0 ] && echo "PASS tests/run_reserved_forms.sh" || echo "FAIL tests/run_reserved_forms.sh"
exit $status
