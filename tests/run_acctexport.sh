#!/usr/bin/env bash
# acctexport (docs/accounting_export_design.md): business records exported for
# an outside accountant under explicit target profiles -- a general bundle,
# QuickBooks Online invoices and bank uploads, Xero invoices, credit notes and
# bank statements, and QuickBooks Desktop IIF -- with a manifest per batch.
#
# THIS CHECKS FILES, NOT IMPORTS. Every layout rule is a vendor's documented
# one (design §3, each with its source and the date it was read); nothing here
# imports a file into QuickBooks or Xero, which is the accountant's step.
#
# Tiers:
#   1. SEMANTICS -- tests/acctexport/acctexport_test.bas, by hand: validation
#      (rounding both ways, totals, discounts, currencies, allocations, credit
#      notes, refunds, payouts, double payout), the plan (unsupported, missing
#      mappings, impossible, lossy), refusals each beside a legal neighbour,
#      determinism and the batch id, the manifest's counts and totals, save.
#      Floor 51 checks, stderr empty.
#   2. LAYOUTS -- tests/acctexport/emit.bas renders every profile from the
#      fixture; tests/acctexport/check_layouts.py (python3's csv and decimal)
#      holds each file to the documented layout and the fixture's hand-worked
#      amounts: headers, required columns, date formats, signs, Intuit's
#      1,000-row and 100-invoice limits, Xero's 500 rows, invoices never split,
#      checksums against the manifest, IIF transactions balancing to zero with
#      the kit's sign conventions. With IIF_KIT_DIR set to Intuit's kit
#      examples, IIF headers are also held to the kit's own files.
#   3. DETERMINISM -- rendered twice, every file byte-identical, the batch ids
#      too.
#   4. EXAMPLES -- examples/acctexport/worksplicer_month.bas runs and writes its
#      batch; every_profile.bas plans, renders three profiles and is refused.
#   5. COOKBOOK -- every ```basic block in docs/acctexport_cookbook.md appears
#      line for line in one of those examples.
#   6. VALGRIND over tier 1.
#
# Tiers 2-3 skip without python3.
. "$(dirname "$0")/portable.sh"
set -u
cd "$(dirname "$0")/.."
. "$(dirname "$0")/valgrind_tier.sh"
export GBASIC_PATH=stdlib

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_acctexport: build failed\n'
    exit 1
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
status=0
fail() { printf 'FAIL %s\n' "$1"; status=1; }

printf -- '-- semantics\n'
mkdir -p "$work/save"
out=$(timeout 300 ./gbasic tests/acctexport/acctexport_test.bas "$work/save" 2>"$work/err")
n=$(printf '%s\n' "$out" | sed -n 's/^checks: \([0-9]*\)$/\1/p')
if [ "$(printf '%s\n' "$out" | tail -1)" = "mismatches: 0" ] && [ "${n:-0}" -ge 51 ] && [ ! -s "$work/err" ]; then
    printf 'PASS semantics (%s checks)\n' "$n"
else
    printf '%s\n' "$out" | grep -v '^ok ' | head -20
    head -5 "$work/err"
    fail "semantics (floor 51 checks, stderr empty)"
fi

if command -v python3 >/dev/null 2>&1; then
    printf -- '-- layouts\n'
    mkdir -p "$work/a" "$work/b"
    if ! timeout 300 ./gbasic tests/acctexport/emit.bas "$work/a" >"$work/emit.a" 2>"$work/emit.err" || [ -s "$work/emit.err" ]; then
        head -5 "$work/emit.err"
        fail "emitting the profiles"
    else
        kit=${IIF_KIT_DIR:-}
        if o=$(python3 -I tests/acctexport/check_layouts.py "$work/a" $kit 2>&1); then
            how="IIF columns as written down from Intuit's kit"
            [ -n "$kit" ] && how="IIF columns held to Intuit's kit files in $kit"
            printf 'PASS layouts (%s checks; %s)\n' "$(printf '%s\n' "$o" | sed -n 's/^checks: //p')" "$how"
        else
            printf '%s\n' "$o" | head -15
            fail "layouts"
        fi
        printf -- '-- determinism\n'
        timeout 300 ./gbasic tests/acctexport/emit.bas "$work/b" >"$work/emit.b" 2>/dev/null
        if diff -r "$work/a" "$work/b" >/dev/null && cmp -s "$work/emit.a" "$work/emit.b"; then
            printf 'PASS determinism (%s batches byte-identical, ids too)\n' "$(wc -l <"$work/emit.a")"
        else
            diff -r "$work/a" "$work/b" | head -5
            fail "determinism"
        fi
    fi
else
    printf 'SKIP layouts and determinism (no python3: nothing was held to a layout from outside)\n'
fi

printf -- '-- example\n'
mkdir -p "$work/ex"
if o=$(timeout 120 ./gbasic examples/acctexport/worksplicer_month.bas "$work/ex" 2>&1) &&
   printf '%s\n' "$o" | grep -q '^valid: true$' &&
   printf '%s\n' "$o" | grep -q '^  total payouts USD 142.44$' &&
   printf '%s\n' "$o" | grep -q '^05/02/2027,142.44,Stripe,Payout from Stripe po_1Q,po_501' &&
   [ -s "$work/ex/manifest.json" ]; then
    printf 'PASS example (worksplicer_month.bas: invoice, payment, fee, payout, the batch written)\n'
else
    printf '%s\n' "$o" | head -20
    fail "example"
fi

if timeout 120 ./gbasic examples/acctexport/every_profile.bas >"$work/every.out" 2>"$work/every.err" && [ ! -s "$work/every.err" ] &&
   grep -q '^qbo.invoices: representable$' "$work/every.out" &&
   grep -q '^05/02/2027,Payout to bank po_X,-116.22' "$work/every.out" &&
   grep -q '^refused: acctexport.render: the profile .qbd.iif. needs what the bundle cannot supply' "$work/every.out"; then
    printf 'PASS example (every_profile.bas: plan, QuickBooks Online, a clearing account, IIF and its refusal)\n'
else
    head -20 "$work/every.out" "$work/every.err"
    fail "example every_profile.bas"
fi

# THE COOKBOOK CANNOT DRIFT FROM CODE THAT RUNS: every ```basic block in
# docs/acctexport_cookbook.md must appear, line for line, in an example this
# suite has just run.
printf -- '-- cookbook\n'
if command -v python3 >/dev/null 2>&1; then
    if o=$(python3 -I - <<'PY' 2>&1
import re, sys
doc = open("docs/acctexport_cookbook.md", encoding="utf-8").read()
blocks = re.findall(r"```basic\n(.*?)\n```", doc, re.S)
srcs = [open(f, encoding="utf-8").read() for f in ["examples/acctexport/worksplicer_month.bas", "examples/acctexport/every_profile.bas"]]
bad = 0
for i, b in enumerate(blocks):
    lines = b.split("\n")
    if not any(any("\n".join((ind + l) if l else "" for l in lines) in s for ind in ("", "    ")) for s in srcs):
        bad += 1
        print("block %d is not in an example: %s" % (i + 1, lines[0][:60]))
print("blocks: %d" % len(blocks))
sys.exit(1 if bad or len(blocks) < 9 else 0)
PY
); then
        printf 'PASS cookbook (%s, each quoted from an example run above)\n' "$(printf '%s\n' "$o" | tail -1)"
    else
        printf '%s\n' "$o" | head -10
        fail "cookbook"
    fi
else
    printf 'SKIP cookbook (no python3)\n'
fi

if vg_available; then
    mkdir -p "$work/vgsave"
    if timeout 900 bash -c '. tests/valgrind_tier.sh; vg_run ./gbasic tests/acctexport/acctexport_test.bas "$1"' _ "$work/vgsave" >/dev/null 2>"$work/vg"; then
        printf 'PASS valgrind tests/acctexport/acctexport_test.bas\n'
    else
        printf 'FAIL valgrind\n'; head -20 "$work/vg"; status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit $status
