#!/usr/bin/env bash
# finio_nacha -- the FIRST ADAPTER, and the first thing the Phase 1 framework
# has ever carried (docs/financial_adapters_design.md §20, §21).
#
# WHY NACHA FIRST. It is fixed-width and record-oriented, which is the shape
# Phase 0's provenance measurement was taken against, so the framework is being
# exercised on the representation whose locations are most expensive. But the
# reason that decided it is that A NACHA FILE CHECKS ITSELF: every batch ends
# with a control record stating its entry count, a hash of the routing numbers
# it touched and its debit and credit totals, and the file ends with one saying
# the same across batches. Those numbers were computed by whoever produced the
# file, so a reader can be held to arithmetic it did not supply -- the same
# property that makes `accounting`'s balance identity and `credit`'s
# reconciliation tests rather than transcripts.
#
# SELF-CHECKING RATHER THAN GOLDEN, and forced harder than Phase 0: every
# defect here is AN ORDINARY-LOOKING PAYMENT FILE. Cents read as dollars is a
# total a hundred times too small; a loan debit counted as a credit balances
# the other way and still balances; a field sliced one byte early is a shorter
# account number that still looks like an account number. A golden would
# record any of them as expected and defend it.
#
# THE EXTERNAL-ORACLE TIER IS THE LOAD-BEARING ONE AND IT IS THREE-WAY. awk
# recomputes the file's totals from the bytes, the file's own control records
# state them, and finio reports them; all three must agree. Two agreeing could
# be one misunderstanding implemented twice -- the fixtures and the adapter
# were both written here -- and awk is a third implementation that has never
# seen either.
set -u
cd "$(dirname "$0")/.."
source tests/valgrind_tier.sh

make >/dev/null || { printf 'FAIL build\n'; exit 1; }
export GBASIC_PATH=stdlib

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
fails=0
ok()  { printf '  ok   %s\n' "$1"; }
bad() { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }

D=tests/finio/nacha

# --- SEMANTICS -------------------------------------------------------------
printf 'TIER semantics\n'
out="$scratch/sem.out"
if timeout 180 ./gbasic tests/finio/nacha_test.bas >"$out" 2>&1; then
    mism="$(sed -n 's/^mismatches: //p' "$out")"
    checks="$(sed -n 's/^checks: //p' "$out")"
    if [ "$mism" = "0" ] && [ "${checks:-0}" -ge 118 ]; then
        ok "$checks checks, 0 mismatches"
    else
        bad "nacha_test: $checks checks, $mism mismatches"
        grep MISMATCH "$out" || true
    fi
else
    bad "nacha_test exited nonzero"; cat "$out"
fi

# --- EXTERNAL LAYOUT ORACLE ------------------------------------------------
# THE ONLY CHECK IN THIS TREE ON THE LAYOUTS THEMSELVES THAT DID NOT COME FROM
# THIS TREE. Every other tier compares the adapter against fixtures this
# project generated from the same understanding of the format the adapter reads
# them with, so a layout wrong in both places agrees with itself perfectly and
# every suite stays green. tests/finio/nacha_positions.txt is a
# field-position table transcribed from a bank's own freely published ACH
# layout guide, 1-based and inclusive as printed; the checker does the
# conversion, because published guides number from 1 and `finio` counts from 0
# and that is the likeliest way to get a fixed-width layout wrong.
printf 'TIER published_layout\n'
pout="$scratch/pos.out"
if timeout 120 ./gbasic tests/finio/nacha_positions_test.bas >"$pout" 2>&1; then
    pm="$(sed -n 's/^mismatches: //p' "$pout")"
    pc="$(sed -n 's/^checks: //p' "$pout")"
    if [ "$pm" = "0" ] && [ "${pc:-0}" -ge 140 ]; then
        ok "$pc checks: every field of all six layouts matches an independently published table"
    else
        bad "published layout: $pc checks, $pm mismatches"
        grep MISMATCH "$pout" || true
    fi
else
    bad "nacha_positions_test exited nonzero"; cat "$pout"
fi

# --- EXTERNAL ORACLE: awk, the control records, and finio ------------------
# awk has never seen this adapter or the generator. It reads the bytes, applies
# the format's own arithmetic, and the three answers must be one answer.
printf 'TIER oracle\n'
awk_totals() {
    awk '
    BEGIN { split("27 28 29 37 38 39 47 48 49 55 56", d, " ")
            split("22 23 24 32 33 34 42 43 44 52 53 54", c, " ")
            for (i in d) isdebit[d[i]] = 1
            for (i in c) iscredit[c[i]] = 1
            recs = 0; batches = 0
            tent = 0; thash = 0; tdebit = 0; tcredit = 0 }
    # A CRLF SOURCE CARRIES THE CR IN $0, which makes every width one byte long
    # and -- because the padding test is anchored -- turns the last all-nines
    # padding record into the file control. That is exactly the class of defect
    # this suite exists to catch, so the oracle must not have it.
    { sub(/\r$/, ""); recs++
      t = substr($0, 1, 1)
      if (t == "5") { batches++; ent = 0; hash = 0; debit = 0; credit = 0 }
      if (t == "6") {
          ent++
          hash += substr($0, 4, 8) + 0
          txn = substr($0, 2, 2)
          amt = substr($0, 30, 10) + 0
          if (isdebit[txn]) debit += amt
          if (iscredit[txn]) credit += amt
      }
      if (t == "7") ent++
      if (t == "8") {
          printf "computed B%d %d %d %d %d\n", batches, ent, hash % 10000000000, debit, credit
          printf "declared B%d %d %d %d %d\n", batches, substr($0, 5, 6) + 0, substr($0, 11, 10) + 0, substr($0, 21, 12) + 0, substr($0, 33, 12) + 0
          tent += ent; thash += hash; tdebit += debit; tcredit += credit
      }
      if (t == "9" && $0 !~ /^9{94}$/) {
          fc = sprintf("declared F %d %d %d %d %d %d", substr($0, 2, 6) + 0, substr($0, 8, 6) + 0, substr($0, 14, 8) + 0, substr($0, 22, 10) + 0, substr($0, 32, 12) + 0, substr($0, 44, 12) + 0)
      }
    }
    END { printf "computed F %d %d %d %d %d %d\n", batches, recs / 10, tent, thash % 10000000000, tdebit, tcredit
          print fc }
    ' "$1"
}
cat > "$scratch/totals.bas" <<'BEOF'
load finio
load finio_nacha
program main( args )
    reg = finio.registry([ finio_nacha.adapter() ])
    doc = finio.read_file(reg, args[0], {})
    ent = 0
    hash = 0
    debit = 0
    credit = 0
    n = 0
    for each b in doc.entities.batches
        n = n + 1
        t = finio_nacha.batch_totals(doc.records, b)
        print ("B" + string(n) + " " + string(t.count) + " "
               + string(t.hash - floor(t.hash / 10000000000) * 10000000000)
               + " " + string(t.debit) + " " + string(t.credit))
        ent = ent + t.count
        hash = hash + t.hash
        debit = debit + t.debit
        credit = credit + t.credit
    end for
    print ("F " + string(n) + " " + string(count(doc.records) / 10)
           + " " + string(ent) + " " + string(hash - floor(hash / 10000000000) * 10000000000)
           + " " + string(debit) + " " + string(credit))
end program
BEOF
for f in payroll crlf blocked; do
    src="$D/$f.ach"
    # The blocked file has no separators at all, so the oracle is given the
    # same bytes cut into 94-byte lines. That is a REFRAMING and not a repair:
    # it is how the file would look line-framed, which is what makes "the same
    # logical file" a checkable claim rather than an assertion.
    if [ "$f" = "blocked" ]; then
        fold -b -w 94 "$src" > "$scratch/blocked_lines.ach"
        src="$scratch/blocked_lines.ach"
    fi
    a="$(awk_totals "$src")"
    computed="$(printf '%s\n' "$a" | sed -n 's/^computed //p')"
    declared="$(printf '%s\n' "$a" | sed -n 's/^declared //p')"
    got="$(timeout 60 ./gbasic "$scratch/totals.bas" "$D/$f.ach" 2>&1)"
    if [ -z "$computed" ] || [ -z "$got" ]; then
        bad "oracle $f: a side produced nothing (awk='$computed' finio='$got')"
    elif [ "$computed" = "$declared" ] && [ "$computed" = "$got" ]; then
        ok "$f: awk, the file's own control records and finio all agree, batch by batch and for the file"
    else
        bad "$f disagreement"
        printf '    awk      : %s\n' "$(printf '%s' "$computed" | tr '\n' ';')"
        printf '    control  : %s\n' "$(printf '%s' "$declared" | tr '\n' ';')"
        printf '    finio    : %s\n' "$(printf '%s' "$got" | tr '\n' ';')"
    fi
done
# AND THE NEGATIVE CONTROL. Without it the tier above is satisfied by three
# implementations wrong in the same way, and by a comparison that never fails:
# the corrupted file must make awk and the control records DISAGREE, and finio
# must side with awk -- that is, with the bytes rather than with the summary.
a="$(awk_totals "$D/bad_total.ach")"
computed="$(printf '%s\n' "$a" | sed -n 's/^computed //p')"
declared="$(printf '%s\n' "$a" | sed -n 's/^declared //p')"
got="$(timeout 60 ./gbasic "$scratch/totals.bas" "$D/bad_total.ach" 2>&1)"
if [ "$computed" != "$declared" ] && [ "$computed" = "$got" ]; then
    ok "CONTROL: the corrupted file makes awk and its own control records differ, and finio sides with the bytes"
else
    bad "control: the corrupted fixture is not corrupted, or finio followed the control record"
    printf '    awk [%s] control [%s] finio [%s]\n' "$computed" "$declared" "$got"
fi

# --- THE THREE FRAMINGS ARE THREE DIFFERENT FILES -------------------------
# The fixture asserts that line-framed, blocked and CRLF sources read
# identically. That is only a claim about framing if they are not the same
# bytes, which the fixture cannot see and this can.
printf 'TIER framing_premise\n'
sizes="$(wc -c <"$D/payroll.ach") $(wc -c <"$D/blocked.ach") $(wc -c <"$D/crlf.ach")"
distinct="$(md5sum "$D/payroll.ach" "$D/blocked.ach" "$D/crlf.ach" | awk '{print $1}' | sort -u | wc -l)"
if [ "$distinct" = "3" ] && [ "$sizes" = "13300 13160 13440" ]; then
    ok "the three framings are three different files ($sizes bytes)"
else
    bad "the framing premise does not hold: $distinct distinct files, sizes $sizes"
fi
if grep -q $'\r' "$D/crlf.ach" && ! grep -q $'\r' "$D/payroll.ach"; then
    ok "and the CRLF one really carries CR where the other does not"
else
    bad "the CRLF fixture is not CRLF"
fi

# --- GENERATOR DRIFT -------------------------------------------------------
# A fixture nothing can reproduce is a fixture that tests itself. The
# generator is also where the control-record arithmetic lives, so this is what
# keeps the oracle above honest as the fixtures grow.
printf 'TIER generator\n'
if command -v python3 >/dev/null 2>&1; then
    python3 tools/make_nacha_fixture.py "$scratch/regen" >/dev/null 2>&1
    if diff -r "$D" "$scratch/regen" >/dev/null 2>&1; then
        ok "the committed fixtures are byte-identical to what the generator writes"
    else
        bad "the committed fixtures have drifted from tools/make_nacha_fixture.py"
        diff -rq "$D" "$scratch/regen" || true
    fi
else
    printf '  SKIP generator drift (no python3)\n'
fi

# --- AN UNREADABLE FILE IS ITS OWN OUTCOME -----------------------------
# `scan` must not end an archive sweep because of one file, and must not count
# it as unrecognised either: an operator deciding what to do about unknown
# files needs to know which of them nobody could open. Asserted as a
# DIFFERENCE -- the unreadable one must be in `unreadable` and NOT in
# `unknown`, or a scan that simply skipped it would pass.
printf 'TIER unreadable\n'
if [ "$(id -u)" = "0" ]; then
    printf '  SKIP unreadable (running as root, where chmod 000 is not a barrier)\n'
else
    mkdir -p "$scratch/arch"
    cp "$D/payroll.ach" "$D/not_ach.txt" "$scratch/arch/"
    printf 'nothing will read this\n' > "$scratch/arch/locked.bin"
    chmod 000 "$scratch/arch/locked.bin"
    cat > "$scratch/scan.bas" <<'BEOF'
load finio
load finio_nacha
program main( args )
    reg = finio.registry([ finio_nacha.adapter() ])
    r = finio.scan(reg, args[0])
    print ("examined " + string(r.examined) + " ach " + string(r.counts["aba.nacha"])
           + " unknown " + string(count(r.unknown)) + " unreadable " + string(count(r.unreadable)))
    for each u in r.unreadable
        print "  locked: " + u.file
    end for
end program
BEOF
    got="$(timeout 60 ./gbasic "$scratch/scan.bas" "$scratch/arch" 2>&1)"
    chmod 644 "$scratch/arch/locked.bin"
    line="$(printf '%s\n' "$got" | sed -n 's/^examined //p')"
    if [ "$line" = "3 ach 1 unknown 1 unreadable 1" ] && printf '%s' "$got" | grep -q 'locked: .*locked.bin'; then
        ok "a scan finishes, counts the unreadable file separately, and names it"
    else
        bad "unreadable: got [$got], wanted examined 3 ach 1 unknown 1 unreadable 1"
    fi
fi

# --- VALGRIND --------------------------------------------------------------
printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/finio/nacha_test.bas >/dev/null 2>&1; then
        ok "no definite leak or invalid access over the adapter"
    else
        bad "valgrind"
    fi
else
    printf '  SKIP valgrind (unavailable)\n'
fi

if [ "$fails" = "0" ]; then
    printf 'run_finio_nacha: all cases passed\n'
else
    printf 'run_finio_nacha: %d FAILED\n' "$fails"
    exit 1
fi
