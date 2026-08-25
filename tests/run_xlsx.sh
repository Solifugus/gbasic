#!/usr/bin/env bash
# xlsx Stages 1-2 — ZIP container, part tree, cells, write + round-trip
# (docs/xlsx_design.md).
#
# Tiers:
#   1. GOLDEN -- read (examples/xlsx_read_test.bas) and write
#      (examples/xlsx_write_test.bas) over the committed fixture. The write
#      golden is the payoff for the part tree: editing ONE sheet must change
#      that part and nothing else, including the vendor part nothing models.
#      Also: save is byte-DETERMINISTIC across runs (ZIP mod-time fields are
#      fixed, not taken from the clock -- a clock there would look like a
#      successful write while quietly making byte comparison useless), and the
#      written container validates under `unzip -t`, i.e. by something other
#      than our own reader.
#   2. RETENTION -- the claim the whole design rests on: the reader discards
#      NOTHING. Every part in the container must appear in xlsx.parts, and the
#      one part nothing models must come back byte-identical. Round-trip write
#      is impossible without this, and a reader that quietly dropped a part
#      would look perfectly healthy until write produced a lesser file months
#      later. Asserted against the ZIP's own entry list, not against a
#      hand-written expectation, so adding a part to the fixture cannot leave
#      the check silently narrower than the file.
#   3. NEGATIVE -- the container errors, each pinned: not a ZIP at all, and a
#      truncated file. A misread container must say so rather than produce a
#      workbook with plausible missing pieces.
#   4. VALGRIND -- the golden under valgrind. A refcounted handle owning a part
#      tree, sheet table and shared-string table is exactly where a leak hides.
#
# Skips cleanly when zlib or libxml2 was unavailable at build time, per the
# project's optional-dependency convention. Needs no python3: the fixture is
# committed, and tools/make_xlsx_fixture.py is only how it was authored.
set -u

cd "$(dirname "$0")/.."

if ! make >/dev/null 2>&1; then
    printf 'FAIL run_xlsx: build failed\n'
    exit 1
fi

out=$(mktemp)
err=$(mktemp)
tmp=$(mktemp -d)
trap 'rm -rf "$out" "$err" "$tmp"' EXIT

FIXTURE=examples/fixtures/xlsx/basic.xlsx
MACRO_FIXTURE=examples/fixtures/xlsx/macro_sheet.xlsx
SHARED_FIXTURE=examples/fixtures/xlsx/shared.xlsx
MODERN_FIXTURE=examples/fixtures/xlsx/modern.xlsx
CROSS_FIXTURE=examples/fixtures/xlsx/crosssheet.xlsx
CHAIN_FIXTURE=examples/fixtures/xlsx/chain.xlsx

# Degrade check: if the module was compiled out, the error must be the clean one.
printf 'program main(args)\n  print xlsx.open("%s")\nend program\n' "$FIXTURE" >"$tmp/probe.bas"
if ./gbasic "$tmp/probe.bas" >/dev/null 2>"$err"; then
    :
elif grep -q 'not available in this build' "$err"; then
    printf 'SKIP run_xlsx (built without zlib or libxml2)\n'
    exit 0
fi

status=0

# --- Tier 1: golden ------------------------------------------------------------
printf -- '-- golden\n'
if timeout 120 ./gbasic examples/xlsx_read_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_read_test.out "$out"; then
        printf 'PASS examples/xlsx_read_test.bas\n'
    else
        printf 'FAIL examples/xlsx_read_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_read_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# --- Tier 1a2: try_open, the non-raising twin ---------------------------------
# Needs a scratch directory because the corpus it walks is built at run time --
# committing four deliberately-broken workbooks would put junk binaries in the
# tree for no gain, and one of them is empty.
printf -- '-- golden: try_open (batch survival + parity with open)\n'
corpus="$tmp/corpus"
mkdir -p "$corpus"
if timeout 120 ./gbasic examples/xlsx_try_open_test.bas "$corpus" >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_try_open_test.out "$out"; then
        printf 'PASS examples/xlsx_try_open_test.bas\n'
    else
        printf 'FAIL examples/xlsx_try_open_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_try_open_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# --- Tier 1b: write + round-trip ----------------------------------------------
printf -- '-- golden: write + round-trip\n'
if timeout 120 ./gbasic examples/xlsx_write_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_write_test.out "$out"; then
        printf 'PASS examples/xlsx_write_test.bas\n'
    else
        printf 'FAIL examples/xlsx_write_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_write_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# Writing is DETERMINISTIC: the same workbook saved twice must produce the same
# bytes. The ZIP header carries mod-time fields, and taking them from the clock
# would make every save differ from the last -- which would look like a
# successful write while quietly making byte comparison useless as a test.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  print xlsx.save(wb, "%s")\nend program\n' \
    "$FIXTURE" "$tmp/det1.xlsx" >"$tmp/det.bas"
timeout 60 ./gbasic "$tmp/det.bas" >/dev/null 2>&1
printf 'program main(args)\n  wb = xlsx.open("%s")\n  print xlsx.save(wb, "%s")\nend program\n' \
    "$FIXTURE" "$tmp/det2.xlsx" >"$tmp/det.bas"
timeout 60 ./gbasic "$tmp/det.bas" >/dev/null 2>&1
if cmp -s "$tmp/det1.xlsx" "$tmp/det2.xlsx"; then
    printf 'PASS save is byte-deterministic across runs\n'
else
    printf 'FAIL save differs between two runs of identical input (a clock in the output?)\n'
    status=1
fi

# The written file must be a valid ZIP by something other than our own reader.
if command -v unzip >/dev/null 2>&1; then
    if unzip -t "$tmp/det1.xlsx" >/dev/null 2>&1; then
        printf 'PASS written container validates under unzip -t\n'
    else
        printf 'FAIL written container does not validate under unzip -t\n'
        status=1
    fi
fi

# --- Tier 1c: the formula evaluator and its oracle ----------------------------
printf -- '-- golden: formula evaluator\n'
if timeout 120 ./gbasic examples/xlsx_formula_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_formula_test.out "$out"; then
        printf 'PASS examples/xlsx_formula_test.bas\n'
    else
        printf 'FAIL examples/xlsx_formula_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_formula_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# ZERO DISAGREEMENTS is the assertion; the golden alone would happily record a
# regression as "the new expected output". A disagreement means the evaluator
# and the cached value differ, which on a real workbook means we are wrong.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  r = xlsx.check(wb, "Formulas")\n  print r.disagree\nend program\n' "$FIXTURE" >"$tmp/chk.bas"
if [ "$(timeout 60 ./gbasic "$tmp/chk.bas" 2>/dev/null)" = "0" ]; then
    printf 'PASS evaluator agrees with every cached value it can judge\n'
else
    printf 'FAIL evaluator disagrees with a cached value\n'
    status=1
fi

# An unimplemented function must be NAMED, never defaulted to a plausible
# number. In a financial model a wrong number that looks right is worse than a
# failure, so this is asserted rather than assumed.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  r = xlsx.check(wb, "Formulas")\n  print r.unsupported\nend program\n' "$FIXTURE" >"$tmp/uns.bas"
if [ "$(timeout 60 ./gbasic "$tmp/uns.bas" 2>/dev/null)" -ge 1 ] 2>/dev/null; then
    printf 'PASS unsupported functions reported by name, not silently zeroed\n'
else
    printf 'FAIL the unsupported-function fixture case is no longer detected\n'
    status=1
fi

# --- Tier 1d: recalculation in dependency order -------------------------------
printf -- '-- golden: recalc\n'
if timeout 120 ./gbasic examples/xlsx_recalc_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_recalc_test.out "$out"; then
        printf 'PASS examples/xlsx_recalc_test.bas\n'
    else
        printf 'FAIL examples/xlsx_recalc_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_recalc_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# ORDER, asserted rather than assumed. On Ledger, D7 = B5*2 sits ABOVE
# B5 = SUM(B2:B3) in sheet order, so an engine evaluating top to bottom hands
# D7 a stale B5 and prints a plausible wrong number. Change B2 to 1000 and the
# only correct transitive answer is D7 = 1801.5; a stale read gives 2302.5.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  xlsx.set(wb, "Ledger", "B2", 1000)\n  xlsx.recalc(wb, "Ledger")\n  print xlsx.cell(wb, "Ledger", "D7").value\nend program\n' "$FIXTURE" >"$tmp/ord.bas"
got=$(timeout 60 ./gbasic "$tmp/ord.bas" 2>/dev/null)
if [ "$got" = "1801.5" ]; then
    printf 'PASS transitive dependent recomputed in the right order\n'
else
    printf 'FAIL transitive dependent = %s (want 1801.5; 2302.5 means a stale input)\n' "$got"
    status=1
fi

# A cycle is REPORTED, and must not take the rest of the sheet with it.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  c = xlsx.recalc(wb, "Circular")\n  print c.circular\n  print xlsx.evaluate(wb, "Circular", "B2")\nend program\n' "$FIXTURE" >"$tmp/cyc.bas"
if [ "$(timeout 60 ./gbasic "$tmp/cyc.bas" 2>/dev/null | tr '\n' ' ')" = "2 15 " ]; then
    printf 'PASS circular reference reported; healthy cells on the sheet still evaluate\n'
else
    printf 'FAIL circular-reference handling changed\n'
    status=1
fi

# --- Tier 2: the reader discards nothing ---------------------------------------
printf -- '-- retention: every container entry survives the read\n'
# The ZIP's own entry names, straight from the central directory, via unzip if
# present and otherwise by scanning the file for entry signatures. Deliberately
# NOT a hardcoded list: the fixture may grow, and the check must grow with it.
if command -v unzip >/dev/null 2>&1; then
    unzip -Z1 "$FIXTURE" 2>/dev/null | sort >"$tmp/want"
else
    printf 'SKIP retention (unzip not installed)\n'
    : >"$tmp/want"
fi
if [ -s "$tmp/want" ]; then
    printf 'program main(args)\n  wb = xlsx.open("%s")\n  for each p in xlsx.parts(wb)\n    print p.name\n  end for\nend program\n' "$FIXTURE" >"$tmp/list.bas"
    if timeout 60 ./gbasic "$tmp/list.bas" 2>"$err" | sort >"$tmp/got"; then
        if diff -u "$tmp/want" "$tmp/got"; then
            printf 'PASS all %s container entries retained\n' "$(wc -l <"$tmp/want" | tr -d ' ')"
        else
            printf 'FAIL part tree does not match the container -- the reader dropped or invented a part\n'
            status=1
        fi
    else
        printf 'FAIL listing parts (exit)\n'
        cat "$err"
        status=1
    fi

    # And the unmodelled part must come back byte-identical, not merely present.
    if command -v unzip >/dev/null 2>&1; then
        unzip -p "$FIXTURE" xl/customData/vendor.xml >"$tmp/want_bytes" 2>/dev/null
        printf 'program main(args)\n  wb = xlsx.open("%s")\n  print to error xlsx.part(wb, "xl/customData/vendor.xml")\nend program\n' "$FIXTURE" >"$tmp/bytes.bas"
        # via stderr so no trailing-newline handling differs from print
        timeout 60 ./gbasic "$tmp/bytes.bas" 2>"$tmp/got_bytes" >/dev/null
        # strip the single trailing newline print adds
        printf '%s' "$(cat "$tmp/got_bytes")" >"$tmp/got_trim"
        printf '%s' "$(cat "$tmp/want_bytes")" >"$tmp/want_trim"
        if cmp -s "$tmp/want_trim" "$tmp/got_trim"; then
            printf 'PASS unmodelled part returned byte-identical\n'
        else
            printf 'FAIL unmodelled part differs from the container copy\n'
            status=1
        fi
    fi
fi

# --- Tier 2b: a sheet with no worksheet part ------------------------------------
#
# Excel writes VBA module and macro sheets as <sheet name="Module1"
# state="veryHidden" r:id=""/>: really in the workbook, nothing behind it. The
# reader used to list such a sheet from xlsx.sheets and then raise "no such
# sheet" from xlsx.cells, which is both self-contradictory and false.
#
# This tier exists because of a MEASUREMENT, not a hunch. Scanning the
# 15,871-workbook Enron corpus (figshare 10.6084/m9.figshare.1221767, CC BY 4.0)
# one workbook per process, 400 files (2.5%) failed, and all 400 were this one
# case -- no ZIP, XML or cell-parsing failure occurred anywhere in the corpus.
# The corpus is not in the test path; this fixture reproduces the shape,
# including the trailing space in "VBACode " that seven of those files carry.
printf -- '-- a sheet that exists with no worksheet part (macro/module sheet)\n'
if timeout 120 ./gbasic examples/xlsx_macro_sheet_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_macro_sheet_test.out "$out"; then
        printf 'PASS examples/xlsx_macro_sheet_test.bas\n'
    else
        printf 'FAIL examples/xlsx_macro_sheet_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_macro_sheet_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# --- Tier 2d: shared formulas ---------------------------------------------------
#
# Excel stores a filled-down formula ONCE and leaves the rest of the run with an
# empty <f t="shared" si="n"/>. Read naively those cells have "a formula whose
# text is empty", which evaluates to #VALUE! -- and because recalc writes values
# back, it CORRUPTED them. 61.0% of formula-bearing workbooks in the corpus use
# shared formulas; 13.2M of 20.7M formula cells are continuations (§13.J).
printf -- '-- shared formulas (a formula filled down, stored once)\n'
if timeout 120 ./gbasic examples/xlsx_shared_formula_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_shared_formula_test.out "$out"; then
        printf 'PASS examples/xlsx_shared_formula_test.bas\n'
    else
        printf 'FAIL examples/xlsx_shared_formula_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_shared_formula_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# THE CORRUPTION REGRESSION, asserted on the saved bytes rather than in memory.
# The failure being guarded is not "a wrong answer" but "a damaged file": recalc
# then save used to persist #VALUE! over every continuation cell.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  xlsx.recalc(wb, "Filled")\n  xlsx.save(wb, "%s")\n  again = xlsx.open("%s")\n  n = 0\n  for each c in xlsx.cells(again, "Filled")\n    if c.value = "#VALUE!" then\n      n = n + 1\n    end if\n  end for\n  print n\n  print xlsx.cell(again, "Filled", "C6").value\nend program\n' \
    "$SHARED_FIXTURE" "$tmp/shared_out.xlsx" "$tmp/shared_out.xlsx" >"$tmp/sh.bas"
sh_res=$(timeout 60 ./gbasic "$tmp/sh.bas" 2>"$err" </dev/null | tr '\n' ' ')
if [ "$sh_res" = "0 60 " ]; then
    printf 'PASS recalc+save leaves no #VALUE! behind (and C6 still 60)\n'
else
    printf 'FAIL recalc+save damaged the shared-formula cells: got "%s" %s\n' "$sh_res" "$(cat "$err")"
    status=1
fi

# --- Tier 2c: NOW / TODAY, the Excel date serial --------------------------------
#
# The largest single coverage win in the corpus (docs/xlsx_design.md §13.I):
# NOW appears in 16.3% of formula-bearing workbooks and makes 1,099 of them
# fully recalculable. It is also the one function whose correctness a golden
# cannot pin, because its whole job is to differ every run.
#
# GBASIC_XLSX_NOW pins the clock (seconds since the Unix epoch, UTC). It exists
# for this tier and nothing else: Excel's epoch is 1899-12-30 rather than
# 1900-01-01 -- Lotus treated 1900 as a leap year and Excel kept the bug for
# compatibility -- and a two-day shift is exactly the error that a "is it a
# plausible number" assertion would never catch. The expected serials below were
# cross-checked against LibreOffice, an independent implementation of the same
# broken epoch, not just re-derived from the same reasoning as the code.
printf -- '-- NOW/TODAY: the Excel date serial (clock pinned)\n'
if TZ=UTC GBASIC_XLSX_NOW=1785758400 timeout 120 ./gbasic tests/xlsx_volatile_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u tests/xlsx_volatile_test.out "$out"; then
        printf 'PASS tests/xlsx_volatile_test.bas\n'
    else
        printf 'FAIL tests/xlsx_volatile_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL tests/xlsx_volatile_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# The epoch, at the dates where it can be wrong. 1900-03-01 is the first day the
# leap-year bug leaves consistent; 2000-02-29 is a real leap day a naive rule
# would drop; 2100-03-01 follows a century year that is NOT a leap year.
serial_case() { # label  epoch-seconds  want-day  want-secs
    local label=$1 when=$2 wd=$3 ws=$4
    local got
    got=$(TZ=UTC GBASIC_XLSX_NOW="$when" timeout 60 ./gbasic tests/xlsx_serial_probe.bas 2>"$err" </dev/null | tr '\n' ' ')
    if [ "$got" = "$wd $ws " ]; then
        printf 'PASS serial %-12s %s -> day %s\n' "$label" "$when" "$wd"
    else
        printf 'FAIL serial %-12s want "%s %s" got "%s" %s\n' "$label" "$wd" "$ws" "$got" "$(cat "$err")"
        status=1
    fi
}
serial_case "1900-03-01" -2203848000  61     43200
serial_case "1970-01-01" 43200        25569  43200
serial_case "2000-02-29" 951825600    36585  43200
serial_case "2026-08-03" 1785758400   46237  43200
serial_case "2100-03-01" 4107585600   73110  43200
# Midnight and one second before it: the fraction must be exactly 0 and 86399,
# not rounded across the day boundary.
serial_case "midnight"   1785715200   46237  0
serial_case "23:59:59"   1785801599   46237  86399

# Excel's NOW() is LOCAL time. Same instant, two zones either side of the date
# line: the day must differ, or the answer is a day out for half the world.
d_utc=$(TZ=UTC             GBASIC_XLSX_NOW=1785801000 timeout 60 ./gbasic tests/xlsx_serial_probe.bas 2>/dev/null | head -1)
d_syd=$(TZ=Australia/Sydney GBASIC_XLSX_NOW=1785801000 timeout 60 ./gbasic tests/xlsx_serial_probe.bas 2>/dev/null | head -1)
if [ -n "$d_utc" ] && [ -n "$d_syd" ] && [ "$d_syd" -eq $((d_utc + 1)) ]; then
    printf 'PASS serial %-12s local time honoured (UTC %s, Sydney %s)\n' "timezone" "$d_utc" "$d_syd"
else
    printf 'FAIL serial %-12s expected Sydney one day ahead; got UTC=%s Sydney=%s\n' "timezone" "$d_utc" "$d_syd"
    status=1
fi

# The REAL clock path, unpinned -- otherwise this whole tier could pass with the
# production path broken and only the test seam working.
real=$(timeout 60 ./gbasic tests/xlsx_serial_probe.bas 2>/dev/null | head -1)
sys=$(date +%s)
want=$(( (sys / 86400) + 25569 ))
if [ -n "$real" ] && [ "$real" -ge $((want - 1)) ] && [ "$real" -le $((want + 1)) ]; then
    printf 'PASS serial %-12s unpinned clock agrees with date(1) (%s)\n' "real clock" "$real"
else
    printf 'FAIL serial %-12s unpinned clock gave %s, date(1) implies ~%s\n' "real clock" "$real" "$want"
    status=1
fi

# --- Tier 2e: SHAPE -- evaluation must not be quadratic in the sheet ------------
#
# Both cell lookups used to be linear scans of every cell, and both run inside
# per-formula loops, so the cost was the PRODUCT. On a real corpus workbook
# (182,752 cells, 50,343 formulas on one sheet) xlsx.check did not finish in 300
# seconds, while merely READING the same file took 0.44s -- so the cost was
# entirely the scans. A (row,col) hash index made that file 2.7s.
#
# Asserted as a RATIO across a 4x size step, never an absolute time: linear is
# ~4x, quadratic ~16x, and the gate sits at 8x. An absolute bound would just
# measure how busy the machine is.
#
# The fixture is generated HERE with awk and zip, so the tier needs no python3
# and nothing large is committed.
printf -- '-- shape: evaluation cost vs sheet size\n'
make_big() { # rows outfile
    local rows=$1 dir=$2
    rm -rf "$dir"; mkdir -p "$dir/_rels" "$dir/xl/_rels" "$dir/xl/worksheets"
    printf '%s' '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>' >"$dir/[Content_Types].xml"
    printf '%s' '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>' >"$dir/_rels/.rels"
    printf '%s' '<?xml version="1.0"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Big" sheetId="1" r:id="rId1"/></sheets></workbook>' >"$dir/xl/workbook.xml"
    printf '%s' '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>' >"$dir/xl/_rels/workbook.xml.rels"
    awk -v n="$rows" 'BEGIN{
        printf "<?xml version=\"1.0\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>";
        for (i=1;i<=n;i++) {
            printf "<row r=\"%d\"><c r=\"A%d\"><v>%d</v></c>", i, i, i;
            # B references A on the same row: one lookup per formula, so a
            # linear lookup makes the sheet quadratic.
            printf "<c r=\"B%d\"><f>A%d*2</f><v>%d</v></c></row>", i, i, i*2;
        }
        printf "</sheetData></worksheet>";
    }' >"$dir/xl/worksheets/sheet1.xml"
    ( cd "$dir" && zip -q -r -X ../"$(basename "$dir")".xlsx . )
}
time_check() { # xlsxfile -> seconds (as integer milliseconds)
    printf 'program main(args)\n  wb = xlsx.open("%s")\n  print xlsx.check(wb, "Big").disagree\nend program\n' "$1" >"$tmp/big.bas"
    local s e
    s=$(date +%s%N)
    timeout 300 ./gbasic "$tmp/big.bas" >/dev/null 2>&1
    e=$(date +%s%N)
    echo $(( (e - s) / 1000000 ))
}
make_big 10000 "$tmp/big1"
make_big 40000 "$tmp/big4"
t1=$(time_check "$tmp/big1.xlsx")
t4=$(time_check "$tmp/big4.xlsx")
if [ "$t1" -lt 5 ]; then t1=5; fi
ratio=$(( t4 * 10 / t1 ))
printf '     4x rows -> %s.%sx time (%sms -> %sms)\n' $((ratio / 10)) $((ratio % 10)) "$t1" "$t4"

# A CEILING is the assertion here, not the ratio, and the reason is measured
# rather than stylistic: with the linear scan restored, this same step reports
# 7.7x -- under an 8x ratio gate, so a ratio test would have PASSED the very
# regression it exists to catch. Fixed startup and linear XML parsing dilute
# the ratio too much at any size this suite can afford.
#
# The absolute numbers on the development machine are 312ms indexed against
# 2415ms scanned, so a 1200ms ceiling sits ~4x above the good case and ~2x
# below the bad one. Generous enough not to flake on a loaded machine, tight
# enough that reverting the index fails it. Both figures are recorded here so a
# later reader can tell whether a new failure means "slower machine" or
# "quadratic is back".
if [ "$t4" -le 1200 ]; then
    printf 'PASS shape 40k-row sheet evaluated in %sms (ceiling 1200ms; linear scan needs ~2400ms)\n' "$t4"
else
    printf 'FAIL shape 40k-row sheet took %sms, over the 1200ms ceiling; the (row,col) index may be gone\n' "$t4"
    status=1
fi


# --- Tier 2e2: ERRAGG -- the IF-criteria family vs error cells in its ranges ----
#
# Found by corpus sampling (2026-08-18): SUMIF($H$9:$H$4991,A6,$D$9:$D$4991)
# returned #N/A where Excel cached 92,800, because 478 dead-lookup #N/A cells
# sat in the SUM RANGE on rows whose criteria did not match. The generic
# pre-dispatch rule -- any error element in any argument propagates -- is
# correct for SUM (Excel agrees) but wrong for the criteria family, which must
# only propagate an error from a cell it actually MATCHED. Also covers the
# other corpus template: a trailing empty argument, SUM(B18:B20,), which Excel
# treats as contributing nothing and we answered with #VALUE!.
#
# The fixture is generated here (awk-free -- it is small) so nothing is
# committed and the committed byte-exact fixtures stay untouched.
printf -- '-- erragg: criteria functions skip unmatched error cells\n'
edir="$tmp/erragg"
rm -rf "$edir"; mkdir -p "$edir/_rels" "$edir/xl/_rels" "$edir/xl/worksheets"
printf '%s' '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>' >"$edir/[Content_Types].xml"
printf '%s' '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>' >"$edir/_rels/.rels"
printf '%s' '<?xml version="1.0"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Agg" sheetId="1" r:id="rId1"/></sheets></workbook>' >"$edir/xl/workbook.xml"
printf '%s' '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>' >"$edir/xl/_rels/workbook.xml.rels"
{
  printf '<?xml version="1.0"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>'
  # x rows carry numbers in B; y rows carry #N/A -- the dead-lookup shape.
  # C1 (an x row) carries #N/A, so a MATCHED error exists in column C.
  printf '<row r="1"><c r="A1" t="inlineStr"><is><t>x</t></is></c><c r="B1"><v>10</v></c><c r="C1" t="e"><v>#N/A</v></c></row>'
  printf '<row r="2"><c r="A2" t="inlineStr"><is><t>y</t></is></c><c r="B2" t="e"><v>#N/A</v></c><c r="C2"><v>2</v></c></row>'
  printf '<row r="3"><c r="A3" t="inlineStr"><is><t>x</t></is></c><c r="B3"><v>20</v></c><c r="C3"><v>5</v></c></row>'
  printf '<row r="4"><c r="A4" t="inlineStr"><is><t>y</t></is></c><c r="B4" t="e"><v>#N/A</v></c><c r="C4"><v>4</v></c></row>'
  printf '<row r="5"><c r="A5" t="inlineStr"><is><t>x</t></is></c><c r="B5"><v>30</v></c><c r="C5"><v>7</v></c></row>'
  printf '<row r="6"><c r="A6" t="inlineStr"><is><t>y</t></is></c><c r="B6" t="e"><v>#N/A</v></c><c r="C6"><v>6</v></c></row>'
  printf '<row r="7">'
  printf '<c r="E1"><f>SUMIF(A1:A6,"x",B1:B6)</f><v>60</v></c>'
  printf '<c r="F1"><f>SUMIF(A1:A6,"x",C1:C6)</f><v>0</v></c>'
  printf '<c r="G1"><f>COUNTIF(A1:A6,"x")</f><v>3</v></c>'
  printf '<c r="H1"><f>SUM(B1:B6)</f><v>0</v></c>'
  printf '<c r="I1"><f>SUMIFS(B1:B6,A1:A6,"x")</f><v>60</v></c>'
  printf '<c r="J1"><f>AVERAGEIF(A1:A6,"x",B1:B6)</f><v>20</v></c>'
  printf '<c r="K1"><f>SUM(B1,B3,)</f><v>30</v></c>'
  printf '<c r="L1"><f>SUM(1,,2)</f><v>3</v></c>'
  printf '</row></sheetData></worksheet>'
} >"$edir/xl/worksheets/sheet1.xml"
( cd "$edir" && zip -q -r -X ../erragg.xlsx . )
cat >"$tmp/erragg.bas" <<'EOB'
program main(args)
  wb = xlsx.open(args[0])
  print "SUMIF skips unmatched errs : " + xlsx.evaluate(wb, "Agg", "E1")
  print "SUMIF matched err spreads  : " + xlsx.evaluate(wb, "Agg", "F1")
  print "COUNTIF unaffected         : " + xlsx.evaluate(wb, "Agg", "G1")
  print "SUM still propagates       : " + xlsx.evaluate(wb, "Agg", "H1")
  print "SUMIFS skips too           : " + xlsx.evaluate(wb, "Agg", "I1")
  print "AVERAGEIF over matches     : " + xlsx.evaluate(wb, "Agg", "J1")
  print "trailing empty argument    : " + xlsx.evaluate(wb, "Agg", "K1")
  print "interior empty argument    : " + xlsx.evaluate(wb, "Agg", "L1")
end program
EOB
erragg_want='SUMIF skips unmatched errs : 60
SUMIF matched err spreads  : #N/A
COUNTIF unaffected         : 3
SUM still propagates       : #N/A
SUMIFS skips too           : 60
AVERAGEIF over matches     : 20
trailing empty argument    : 30
interior empty argument    : 3'
erragg_got=$(timeout 60 ./gbasic "$tmp/erragg.bas" "$tmp/erragg.xlsx" 2>&1)
if [ "$erragg_got" = "$erragg_want" ]; then
    printf 'PASS erragg criteria family vs error cells (and empty arguments)\n'
else
    printf 'FAIL erragg\n'
    diff <(printf '%s\n' "$erragg_want") <(printf '%s\n' "$erragg_got") || true
    status=1
fi

# --- Tier 2e3: DEFNAMES -- defined names spliced at the lexer -------------------
#
# Found by corpus sampling (2026-08-18): a bare defined name (EffDt, Date,
# mrg_output) fell into the function-call path, which consumed the tokens after
# it and answered #VALUE!. Because that path never recorded a NAME it refused,
# the whole class was invisible to the blockers ranking -- and the ranker's
# "FUNC DATE, 66,735 cells" was the defined name `Date` upper-cased into the
# DATE function's bucket. The dominant books-share of the remaining err->num
# disagreements.
#
# The fix is a LEXER-LEVEL SPLICE of the name's refersTo text, not evaluation
# to a value: `Holidays` naming $A$1:$A$3 must arrive at SUM's argument
# collector as REF COLON REF so it flattens as a range, which a value-level
# expansion cannot do. A resume-stack depth cap doubles as the cycle guard.
#
# CAVEAT, as with basic.xlsx: cached values here are hand-written, so check
# measures self-consistency; the corpus re-measure afterward is the oracle.
printf -- '-- defnames: defined names resolve by textual splice\n'
ndir="$tmp/defnames"
rm -rf "$ndir"; mkdir -p "$ndir/_rels" "$ndir/xl/_rels" "$ndir/xl/worksheets"
printf '%s' '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>' >"$ndir/[Content_Types].xml"
printf '%s' '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>' >"$ndir/_rels/.rels"
# Two sheets so localSheetId scoping is real: `Scoped` is 10 globally and 20
# local to sheet index 1 (Two). `Date` is a defined name that SPELLS a function.
printf '%s' '<?xml version="1.0"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="N" sheetId="1" r:id="rId1"/><sheet name="Two" sheetId="2" r:id="rId2"/></sheets><definedNames><definedName name="TaxRate">0.05</definedName><definedName name="EffDt">N!$B$1</definedName><definedName name="Vals">N!$A$1:$A$3</definedName><definedName name="Gross">N!$B$2</definedName><definedName name="Fees">N!$B$3</definedName><definedName name="Net">Gross-Fees</definedName><definedName name="Loop1">Loop2</definedName><definedName name="Loop2">Loop1</definedName><definedName name="Date">N!$B$1</definedName><definedName name="Scoped">10</definedName><definedName name="Scoped" localSheetId="1">20</definedName><definedName name="Dbl">N!$A$6</definedName></definedNames></workbook>' >"$ndir/xl/workbook.xml"
printf '%s' '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet2.xml"/></Relationships>' >"$ndir/xl/_rels/workbook.xml.rels"
{
  printf '<?xml version="1.0"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>'
  # D1 sits in ROW 1 and depends -- only through the name Dbl -- on A6, a
  # formula in ROW 6. Cell-order evaluation hands D1 a stale A6, so after
  # changing B6 the cached D1 says 15 where the only correct answer is 101.
  # That is the assertion that fails if the dependency walker cannot see
  # through a defined name.
  printf '<row r="1"><c r="A1"><v>1</v></c><c r="B1"><v>44000</v></c><c r="C1"><f>TaxRate*100</f><v>5</v></c><c r="D1"><f>Dbl+1</f><v>15</v></c></row>'
  printf '<row r="2"><c r="A2"><v>2</v></c><c r="B2"><v>100</v></c><c r="C2"><f>EffDt</f><v>44000</v></c></row>'
  printf '<row r="3"><c r="A3"><v>3</v></c><c r="B3"><v>15</v></c><c r="C3"><f>SUM(Vals)</f><v>6</v></c></row>'
  printf '<row r="4"><c r="C4"><f>Net</f><v>85</v></c></row>'
  printf '<row r="5"><c r="C5"><f>Loop1+1</f><v>0</v></c></row>'
  printf '<row r="6"><c r="A6"><f>B6*2</f><v>14</v></c><c r="B6"><v>7</v></c><c r="C6"><f>NoSuchName</f><v>0</v></c></row>'
  printf '<row r="7"><c r="C7"><f>taxrate*200</f><v>10</v></c></row>'
  printf '<row r="8"><c r="C8"><f>Date+1</f><v>44001</v></c><c r="C9"><f>Scoped</f><v>10</v></c></row>'
  printf '</sheetData></worksheet>'
} >"$ndir/xl/worksheets/sheet1.xml"
printf '%s' '<?xml version="1.0"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1"><f>Scoped</f><v>20</v></c></row></sheetData></worksheet>' >"$ndir/xl/worksheets/sheet2.xml"
( cd "$ndir" && zip -q -r -X ../defnames.xlsx . )
cat >"$tmp/defnames.bas" <<'EOB'
program main(args)
  wb = xlsx.open(args[0])
  print "constant name        : " + xlsx.evaluate(wb, "N", "C1")
  print "cell name            : " + xlsx.evaluate(wb, "N", "C2")
  print "range name in SUM    : " + xlsx.evaluate(wb, "N", "C3")
  print "nested names         : " + xlsx.evaluate(wb, "N", "C4")
  print "cycle is an error    : " + xlsx.evaluate(wb, "N", "C5")
  print "unknown name         : " + xlsx.evaluate(wb, "N", "C6")
  print "case-insensitive     : " + xlsx.evaluate(wb, "N", "C7")
  print "name spelled Date    : " + xlsx.evaluate(wb, "N", "C8")
  print "global scope         : " + xlsx.evaluate(wb, "N", "C9")
  print "local scope wins     : " + xlsx.evaluate(wb, "Two", "A1")
  xlsx.set(wb, "N", "B6", 50)
  xlsx.recalc(wb, "N")
  print "recalc through name  : " + xlsx.cell(wb, "N", "D1").value
end program
EOB
defnames_want='constant name        : 5
cell name            : 44000
range name in SUM    : 6
nested names         : 85
cycle is an error    : #NAME?
unknown name         : #NAME?
case-insensitive     : 10
name spelled Date    : 44001
global scope         : 10
local scope wins     : 20
recalc through name  : 101'
defnames_got=$(timeout 60 ./gbasic "$tmp/defnames.bas" "$tmp/defnames.xlsx" 2>&1)
if [ "$defnames_got" = "$defnames_want" ]; then
    printf 'PASS defnames splice: constants, cells, ranges, nesting, scope, cycle, recalc\n'
else
    printf 'FAIL defnames\n'
    diff <(printf '%s\n' "$defnames_want") <(printf '%s\n' "$defnames_got") || true
    status=1
fi

# check must skip the two cells only WE cannot answer (the cycle and the
# unknown name -- both reported as unsupported BY NAME, which is what made
# this class visible at last) and disagree on nothing it does judge. A fresh
# open, because the recalc above legitimately moved cached values.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  r = xlsx.check(wb, "N")\n  print r.disagree\n  print r.unsupported\nend program\n' "$tmp/defnames.xlsx" >"$tmp/defchk.bas"
defchk=$(timeout 60 ./gbasic "$tmp/defchk.bas" 2>/dev/null | tr '\n' ' ')
if [ "$defchk" = "0 2 " ]; then
    printf 'PASS defnames check: zero disagreements, cycle+unknown reported unsupported\n'
else
    printf 'FAIL defnames check: disagree/unsupported were "%s", want "0 2"\n' "$defchk"
    status=1
fi

# --- Tier 2e4: INTERSECT -- implicit intersection for ranges in scalar slots ----
#
# Surfaced by the defined-names splice on the same corpus sweep that shipped
# it: IF(REF_DT<=LastDay,...) where REF_DT names a whole column. That is not
# an array formula -- Excel's pre-dynamic-array rule takes the element of the
# range on the formula's OWN row (single-column range) or own column
# (single-row range). We answered the comparison against the range's FIRST
# endpoint: a plausible boolean where Excel cached a number, 144k cells in
# one workbook family, invisible before the splice because these cells died
# anonymously as unknown functions and were never judged at all.
#
# Two code paths carry it: a range reaching the expression parser directly
# (a bare =ICol, and literal $A$1:$A$5*2), and the argument collector
# noticing the token AFTER a consumed range is an OPERATOR -- meaning the
# range was not a whole argument but the start of a scalar expression -- and
# rewinding. The two IF rows assert intersection happens PER ROW: same
# formula, different rows, different branches. Also pins SUM(I!$7:$7)=15:
# the whole-row span started at column 1 in a zero-based column scheme,
# silently skipping column A.
printf -- '-- intersect: a range in a scalar slot takes the element on its own row\n'
idir="$tmp/isect"
rm -rf "$idir"; mkdir -p "$idir/_rels" "$idir/xl/_rels" "$idir/xl/worksheets"
printf '%s' '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>' >"$idir/[Content_Types].xml"
printf '%s' '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>' >"$idir/_rels/.rels"
printf '%s' '<?xml version="1.0"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="I" sheetId="1" r:id="rId1"/></sheets><definedNames><definedName name="ICol">I!$A$1:$A$5</definedName><definedName name="IRow">I!$A$7:$E$7</definedName><definedName name="IRect">I!$A$1:$B$5</definedName></definedNames></workbook>' >"$idir/xl/workbook.xml"
printf '%s' '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>' >"$idir/xl/_rels/workbook.xml.rels"
{
  printf '<?xml version="1.0"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>'
  printf '<row r="1"><c r="A1"><v>10</v></c><c r="B1"><v>1</v></c><c r="C1"><f>IF(ICol&lt;=25,111,222)</f><v>111</v></c></row>'
  printf '<row r="2"><c r="A2"><v>20</v></c><c r="B2"><v>2</v></c><c r="C2"><f>ICol</f><v>20</v></c><c r="D2"><f>IRow+1</f><v>5</v></c><c r="E2"><f>$A$1:$A$5*2</f><v>40</v></c></row>'
  printf '<row r="3"><c r="A3"><v>30</v></c><c r="C3"><f>ICol*10</f><v>300</v></c></row>'
  printf '<row r="4"><c r="A4"><v>40</v></c><c r="C4"><f>IF(ICol&lt;=25,111,222)</f><v>222</v></c></row>'
  printf '<row r="5"><c r="A5"><v>50</v></c></row>'
  printf '<row r="6"><c r="C6" t="e"><f>ICol</f><v>#VALUE!</v></c></row>'
  printf '<row r="7"><c r="A7"><v>1</v></c><c r="B7"><v>2</v></c><c r="C7"><v>3</v></c><c r="D7"><v>4</v></c><c r="E7"><v>5</v></c></row>'
  printf '<row r="8"><c r="C8" t="e"><f>IRect</f><v>#VALUE!</v></c><c r="D8"><f>SUM(I!$7:$7)</f><v>15</v></c></row>'
  printf '</sheetData></worksheet>'
} >"$idir/xl/worksheets/sheet1.xml"
( cd "$idir" && zip -q -r -X ../isect.xlsx . )
cat >"$tmp/isect.bas" <<'EOB'
program main(args)
  wb = xlsx.open(args[0])
  print "column name, bare     : " + xlsx.evaluate(wb, "I", "C2")
  print "column name, in arith : " + xlsx.evaluate(wb, "I", "C3")
  print "row name intersects   : " + xlsx.evaluate(wb, "I", "D2")
  print "literal range, scalar : " + xlsx.evaluate(wb, "I", "E2")
  print "IF arg, row 1 branch  : " + xlsx.evaluate(wb, "I", "C1")
  print "IF arg, row 4 branch  : " + xlsx.evaluate(wb, "I", "C4")
  print "row outside the range : " + xlsx.evaluate(wb, "I", "C6")
  print "rectangle refuses     : " + xlsx.evaluate(wb, "I", "C8")
  print "whole-row includes A  : " + xlsx.evaluate(wb, "I", "D8")
end program
EOB
isect_want='column name, bare     : 20
column name, in arith : 300
row name intersects   : 5
literal range, scalar : 40
IF arg, row 1 branch  : 111
IF arg, row 4 branch  : 222
row outside the range : #VALUE!
rectangle refuses     : #VALUE!
whole-row includes A  : 15'
isect_got=$(timeout 60 ./gbasic "$tmp/isect.bas" "$tmp/isect.xlsx" 2>&1)
if [ "$isect_got" = "$isect_want" ]; then
    printf 'PASS intersect: per-row collapse, both code paths, refusals, whole-row from A\n'
else
    printf 'FAIL intersect\n'
    diff <(printf '%s\n' "$isect_want") <(printf '%s\n' "$isect_got") || true
    status=1
fi

# Everything on the sheet -- including the two #VALUE! cells, which are
# Excel's answer rather than a refusal -- must agree with the cached values.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  r = xlsx.check(wb, "I")\n  print r.disagree\n  print r.unsupported\nend program\n' "$tmp/isect.xlsx" >"$tmp/isectchk.bas"
isectchk=$(timeout 60 ./gbasic "$tmp/isectchk.bas" 2>/dev/null | tr '\n' ' ')
if [ "$isectchk" = "0 0 " ]; then
    printf 'PASS intersect check: zero disagreements, nothing unsupported\n'
else
    printf 'FAIL intersect check: disagree/unsupported were "%s", want "0 0"\n' "$isectchk"
    status=1
fi

# --- Tier 2e5: COERCE -- the empty STRING does not coerce to a number -----------
#
# Found by corpus sampling (2026-08-19, §13.AB): the EOL_All_Swaps family
# fills down DATE(LEFT(I4,4),MID(I4,5,2),MID(I4,7,2)) over rows whose source
# cell is empty. LEFT("",4) is "", and xlsx_as_num coerced "" to 0 -- so DATE
# range-checked 0/0/0 to #NUM! where Excel, refusing to coerce non-numeric
# text (""+1 is #VALUE!), cached #VALUE!. ~65k cells with the right answer
# SHAPE and the wrong error name. The rule now: the empty STRING fails
# numeric coercion; the empty CELL still coerces to 0; numeric text still
# coerces. The pins matter as much as the fixes -- SUM over a range holding
# a text cell must still skip it (aggregates filter by KIND and never
# coerce), and a real date built from real text must still build.
printf -- '-- coerce: "" refuses, empty cell is 0, numeric text still coerces\n'
codir="$tmp/coerce"
rm -rf "$codir"; mkdir -p "$codir/_rels" "$codir/xl/_rels" "$codir/xl/worksheets"
printf '%s' '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>' >"$codir/[Content_Types].xml"
printf '%s' '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>' >"$codir/_rels/.rels"
printf '%s' '<?xml version="1.0"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="C" sheetId="1" r:id="rId1"/></sheets></workbook>' >"$codir/xl/workbook.xml"
printf '%s' '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>' >"$codir/xl/_rels/workbook.xml.rels"
{
  printf '<?xml version="1.0"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>'
  printf '<row r="1"><c r="A1"><v>5</v></c><c r="D1"><v>1</v></c><c r="C1" t="e"><f>""+1</f><v>#VALUE!</v></c></row>'
  printf '<row r="2"><c r="A2" t="inlineStr"><is><t>20011231</t></is></c><c r="D2" t="inlineStr"><is><t></t></is></c><c r="C2" t="e"><f>DATE(LEFT(A3,4),MID(A3,5,2),MID(A3,7,2))</f><v>#VALUE!</v></c></row>'
  printf '<row r="3"><c r="A3" t="inlineStr"><is><t></t></is></c><c r="D3"><v>2</v></c><c r="C3"><f>B9+1</f><v>1</v></c></row>'
  printf '<row r="4"><c r="C4"><f>"5"+1</f><v>6</v></c></row>'
  printf '<row r="5"><c r="C5" t="e"><f>-""</f><v>#VALUE!</v></c></row>'
  printf '<row r="6"><c r="C6"><f>DATE(LEFT(A2,4),MID(A2,5,2),MID(A2,7,2))</f><v>37256</v></c></row>'
  printf '<row r="7"><c r="C7"><f>SUM(D1:D3)</f><v>3</v></c></row>'
  printf '<row r="8"><c r="C8" t="e"><f>ROUND("",2)</f><v>#VALUE!</v></c></row>'
  printf '</sheetData></worksheet>'
} >"$codir/xl/worksheets/sheet1.xml"
( cd "$codir" && zip -q -r -X ../coerce.xlsx . )
cat >"$tmp/coerce.bas" <<'EOB'
program main(args)
  wb = xlsx.open(args[0])
  print "empty string + 1      : " + xlsx.evaluate(wb, "C", "C1")
  print "the corpus DATE shape : " + xlsx.evaluate(wb, "C", "C2")
  print "empty CELL + 1        : " + xlsx.evaluate(wb, "C", "C3")
  print "numeric text coerces  : " + xlsx.evaluate(wb, "C", "C4")
  print "unary minus on empty  : " + xlsx.evaluate(wb, "C", "C5")
  print "real date still works : " + xlsx.evaluate(wb, "C", "C6")
  print "SUM skips text in rng : " + xlsx.evaluate(wb, "C", "C7")
  print "ROUND refuses empty   : " + xlsx.evaluate(wb, "C", "C8")
end program
EOB
coerce_want='empty string + 1      : #VALUE!
the corpus DATE shape : #VALUE!
empty CELL + 1        : 1
numeric text coerces  : 6
unary minus on empty  : #VALUE!
real date still works : 37256
SUM skips text in rng : 3
ROUND refuses empty   : #VALUE!'
coerce_got=$(timeout 60 ./gbasic "$tmp/coerce.bas" "$tmp/coerce.xlsx" 2>&1)
if [ "$coerce_got" = "$coerce_want" ]; then
    printf 'PASS coerce: "" refuses everywhere Excel refuses, the pins hold\n'
else
    printf 'FAIL coerce\n'
    diff <(printf '%s\n' "$coerce_want") <(printf '%s\n' "$coerce_got") || true
    status=1
fi

printf 'program main(args)\n  wb = xlsx.open("%s")\n  r = xlsx.check(wb, "C")\n  print r.disagree\n  print r.unsupported\nend program\n' "$tmp/coerce.xlsx" >"$tmp/coercechk.bas"
coercechk=$(timeout 60 ./gbasic "$tmp/coercechk.bas" 2>/dev/null | tr '\n' ' ')
if [ "$coercechk" = "0 0 " ]; then
    printf 'PASS coerce check: zero disagreements, nothing unsupported\n'
else
    printf 'FAIL coerce check: disagree/unsupported were "%s", want "0 0"\n' "$coercechk"
    status=1
fi

# --- Tier 2e6: RESIDUE -- deleted-ref literals + the empty-cell lookup key ------
#
# Two corpus classes sampled 2026-08-19 (§13.AC), one fixture:
#
# (1) Excel rewrites a reference whose target was DELETED into a literal
# #REF! in the formula TEXT -- GRMSDetail!#REF! -- and the sheet-qualified
# lexer path could not tokenise '#' after '!', so the whole formula answered
# #VALUE! where Excel caches #REF!. ~6,900 cells per book across the
# joe_parks Position-report family; the largest slice of #VALUE!->err.
#
# (2) An EMPTY-CELL lookup key is the number 0 in VLOOKUP/HLOOKUP/MATCH --
# Excel's rule, and real workbooks lean on it: the DYNEGY-ICE family keeps a
# sentinel row (0 -> "No Activity") precisely so unfilled keys resolve to
# it. 39k cells answered #N/A where Excel cached that text. The PIN matters:
# an empty STRING key is NOT 0 (Excel does not coerce "" here either), so
# VLOOKUP("",...) must stay #N/A.
printf -- '-- residue: Sheet!#REF! literals lex; empty-cell lookup keys are 0\n'
rdir="$tmp/residue"
rm -rf "$rdir"; mkdir -p "$rdir/_rels" "$rdir/xl/_rels" "$rdir/xl/worksheets"
printf '%s' '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>' >"$rdir/[Content_Types].xml"
printf '%s' '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>' >"$rdir/_rels/.rels"
printf '%s' '<?xml version="1.0"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="R" sheetId="1" r:id="rId1"/></sheets></workbook>' >"$rdir/xl/workbook.xml"
printf '%s' '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>' >"$rdir/xl/_rels/workbook.xml.rels"
{
  printf '<?xml version="1.0"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>'
  printf '<row r="1"><c r="E1" t="inlineStr"><is><t>AA</t></is></c><c r="F1" t="inlineStr"><is><t>a</t></is></c><c r="C1" t="inlineStr"><f>VLOOKUP(B9,E1:F3,2,FALSE)</f><is><t>No Activity</t></is></c></row>'
  printf '<row r="2"><c r="E2"><v>0</v></c><c r="F2" t="inlineStr"><is><t>No Activity</t></is></c><c r="C2"><f>MATCH(B9,E1:E3,0)</f><v>2</v></c></row>'
  printf '<row r="3"><c r="E3" t="inlineStr"><is><t>BB</t></is></c><c r="F3" t="inlineStr"><is><t>b</t></is></c><c r="C3" t="e"><f>R!#REF!</f><v>#REF!</v></c></row>'
  printf '<row r="4"><c r="C4" t="e"><f>&#39;R&#39;!#REF!</f><v>#REF!</v></c></row>'
  printf '<row r="5"><c r="C5" t="e"><f>SUM(R!#REF!,E2)</f><v>#REF!</v></c></row>'
  printf '<row r="6"><c r="C6" t="e"><f>VLOOKUP("",E1:F3,2,FALSE)</f><v>#N/A</v></c></row>'
  printf '</sheetData></worksheet>'
} >"$rdir/xl/worksheets/sheet1.xml"
( cd "$rdir" && zip -q -r -X ../residue.xlsx . )
cat >"$tmp/residue.bas" <<'EOB'
program main(args)
  wb = xlsx.open(args[0])
  print "empty key finds the 0 : " + xlsx.evaluate(wb, "R", "C1")
  print "MATCH does too        : " + xlsx.evaluate(wb, "R", "C2")
  print "Sheet!#REF! literal   : " + xlsx.evaluate(wb, "R", "C3")
  print "quoted variant        : " + xlsx.evaluate(wb, "R", "C4")
  print "propagates through SUM: " + xlsx.evaluate(wb, "R", "C5")
  print "empty STRING is not 0 : " + xlsx.evaluate(wb, "R", "C6")
end program
EOB
residue_want='empty key finds the 0 : No Activity
MATCH does too        : 2
Sheet!#REF! literal   : #REF!
quoted variant        : #REF!
propagates through SUM: #REF!
empty STRING is not 0 : #N/A'
residue_got=$(timeout 60 ./gbasic "$tmp/residue.bas" "$tmp/residue.xlsx" 2>&1)
if [ "$residue_got" = "$residue_want" ]; then
    printf 'PASS residue: deleted-ref literals answer #REF!, empty-cell keys find the 0 row\n'
else
    printf 'FAIL residue\n'
    diff <(printf '%s\n' "$residue_want") <(printf '%s\n' "$residue_got") || true
    status=1
fi

printf 'program main(args)\n  wb = xlsx.open("%s")\n  r = xlsx.check(wb, "R")\n  print r.disagree\n  print r.unsupported\nend program\n' "$tmp/residue.xlsx" >"$tmp/residuechk.bas"
residuechk=$(timeout 60 ./gbasic "$tmp/residuechk.bas" 2>/dev/null | tr '\n' ' ')
if [ "$residuechk" = "0 0 " ]; then
    printf 'PASS residue check: zero disagreements, nothing unsupported\n'
else
    printf 'FAIL residue check: disagree/unsupported were "%s", want "0 0"\n' "$residuechk"
    status=1
fi

# --- Tier 2e7: TRI -- qualified names, array refusal, empty criteria ------------
#
# Three corpus classes sampled 2026-08-19 (§13.AD), one fixture:
#
# (1) A SHEET-QUALIFIED DEFINED NAME -- EO9904.2!mthend, the qualifier being a
# SCOPE hint (that sheet's local name first, else global). It passed through
# the lexer as an unparseable REF and the cell read failed: the largest slice
# of #REF!->num. Range endpoints (Data!A:B) must keep passing through, so
# nothing range-shaped (letters <= 3, or digits) is ever treated as a name.
#
# (2) A CSE ARRAY FORMULA (<f t="array">SUM(IF(NG=C4,D4))) evaluated with
# scalar semantics gives a plausible WRONG NUMBER -- implicit intersection
# turns the array comparison into a per-row one. Refused BY NAME ("array
# formula"), cached values left untouched by recalc, until array semantics
# exist. The bulk of #VALUE!->num came from exactly this idiom.
#
# (3) A criteria that is a reference to an EMPTY CELL is 0 -- the same rule
# the lookup key follows. Textified naively it was "", which matched every
# BLANK cell in the range: 9,351 counted where Excel cached 0.
printf -- '-- tri: Sheet!name scope, array-formula refusal, empty criteria is 0\n'
tdir="$tmp/tri"
rm -rf "$tdir"; mkdir -p "$tdir/_rels" "$tdir/xl/_rels" "$tdir/xl/worksheets"
printf '%s' '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/><Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>' >"$tdir/[Content_Types].xml"
printf '%s' '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>' >"$tdir/_rels/.rels"
printf '%s' '<?xml version="1.0"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Q" sheetId="1" r:id="rId1"/><sheet name="EO" sheetId="2" r:id="rId2"/></sheets><definedNames><definedName name="mthend">EO!$A$1</definedName><definedName name="loco" localSheetId="1">EO!$A$2</definedName></definedNames></workbook>' >"$tdir/xl/workbook.xml"
printf '%s' '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/><Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet2.xml"/></Relationships>' >"$tdir/xl/_rels/workbook.xml.rels"
{
  printf '<?xml version="1.0"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>'
  printf '<row r="1"><c r="A1"><v>1</v></c><c r="B1"><v>3</v></c><c r="F1"><v>1</v></c><c r="G1"><v>0</v></c><c r="C1"><f>EO!mthend*2</f><v>200</v></c><c r="D1"><f t="array" ref="D1">SUM(IF(A1:A3=1,B1:B3))</f><v>10</v></c></row>'
  printf '<row r="2"><c r="A2"><v>2</v></c><c r="B2"><v>5</v></c><c r="F2"><v>2</v></c><c r="C2"><f>EO!loco+1</f><v>8</v></c></row>'
  printf '<row r="3"><c r="A3"><v>1</v></c><c r="B3"><v>7</v></c><c r="F3"><v>4</v></c><c r="G3" t="inlineStr"><is><t>x</t></is></c><c r="C3"><f>COUNTIF(G1:G4,H9)</f><v>2</v></c></row>'
  printf '<row r="4"><c r="F4"><v>8</v></c><c r="G4"><v>0</v></c><c r="C4"><f>SUMIFS(F1:F4,G1:G4,H9)</f><v>9</v></c></row>'
  printf '</sheetData></worksheet>'
} >"$tdir/xl/worksheets/sheet1.xml"
printf '%s' '<?xml version="1.0"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData><row r="1"><c r="A1"><v>100</v></c></row><row r="2"><c r="A2"><v>7</v></c></row></sheetData></worksheet>' >"$tdir/xl/worksheets/sheet2.xml"
( cd "$tdir" && zip -q -r -X ../tri.xlsx . )
cat >"$tmp/tri.bas" <<'EOB'
program main(args)
  wb = xlsx.open(args[0])
  print "Sheet!name resolves    : " + xlsx.evaluate(wb, "Q", "C1")
  print "qualifier finds local  : " + xlsx.evaluate(wb, "Q", "C2")
  print "array formula refused  : " + xlsx.evaluate(wb, "Q", "D1")
  print "empty criteria means 0 : " + xlsx.evaluate(wb, "Q", "C3")
  print "SUMIFS agrees          : " + xlsx.evaluate(wb, "Q", "C4")
  r = xlsx.check(wb, "Q")
  print "check disagree         : " + string(r.disagree)
  print "check unsupported      : " + string(r.unsupported)
end program
EOB
tri_want='Sheet!name resolves    : 200
qualifier finds local  : 8
array formula refused  : (not evaluated: array formula)
empty criteria means 0 : 2
SUMIFS agrees          : 9
check disagree         : 0
check unsupported      : 1'
tri_got=$(timeout 60 ./gbasic "$tmp/tri.bas" "$tmp/tri.xlsx" 2>&1)
if [ "$tri_got" = "$tri_want" ]; then
    printf 'PASS tri: qualified names splice, arrays refused by name, empty criteria is 0\n'
else
    printf 'FAIL tri\n'
    diff <(printf '%s\n' "$tri_want") <(printf '%s\n' "$tri_got") || true
    status=1
fi

# recalc must leave an array cell's CACHED value untouched -- writing back a
# scalar mis-evaluation is the shared-formula corruption hazard again.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  xlsx.recalc(wb, "Q")\n  print xlsx.cell(wb, "Q", "D1").value\nend program\n' "$tmp/tri.xlsx" >"$tmp/trirc.bas"
trirc=$(timeout 60 ./gbasic "$tmp/trirc.bas" 2>/dev/null)
if [ "$trirc" = "10" ]; then
    printf 'PASS tri recalc: the array cell keeps its cached value\n'
else
    printf 'FAIL tri recalc: array cell cached value became "%s", want 10\n' "$trirc"
    status=1
fi

# --- Tier 2e8: EXTD -- external workbook NAMES + month-name text dates ----------
#
# Two corpus classes sampled 2026-08-19 (§13.AE), one fixture:
#
# (1) [1]!Table names a DEFINED NAME in an EXTERNAL workbook. Two defects
# stacked: xlsx_cell_value_in parsed the ref BEFORE its external check, so a
# name-shaped ref bounced to a bare #REF! that never set the unsupported flag
# (the class counted as wrong ANSWERS, not priced refusals -- [1]Book1!A1
# parsed and was reported fine, which kept the gap invisible); and the
# sheet-qualified name splice did not veto external qualifiers, so a COLLIDING
# internal name answered with the WRONG WORKBOOK's value -- the fixture's
# shadow case (internal Table = 42) proved that red before the guard existed.
#
# (2) Text dates with MONTH NAMES -- MONTH("01-JAN-2001") answered #VALUE!
# where Excel coerces DD-MMM-YYYY (7,484 cells in one book). English only,
# deliberately: the corpus is US English and other locales' names would trade
# a visible refusal for a silent wrong month. Two-digit years pivot at 30.
printf -- '-- extd: external names priced not judged; DD-MMM-YYYY coerces\n'
edir2="$tmp/extd"
rm -rf "$edir2"; mkdir -p "$edir2/_rels" "$edir2/xl/_rels" "$edir2/xl/worksheets"
printf '%s' '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="xml" ContentType="application/xml"/><Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/><Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/></Types>' >"$edir2/[Content_Types].xml"
printf '%s' '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>' >"$edir2/_rels/.rels"
printf '%s' '<?xml version="1.0"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="X" sheetId="1" r:id="rId1"/></sheets><definedNames><definedName name="Table">X!$E$1</definedName></definedNames></workbook>' >"$edir2/xl/workbook.xml"
printf '%s' '<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>' >"$edir2/xl/_rels/workbook.xml.rels"
{
  printf '<?xml version="1.0"?><worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData>'
  printf '<row r="1"><c r="A1"><v>1</v></c><c r="E1"><v>42</v></c><c r="B1"><f>[1]!Table</f><v>6.158</v></c><c r="C1"><f>MONTH("01-JAN-2001")</f><v>1</v></c></row>'
  printf '<row r="2"><c r="B2"><f>VLOOKUP($A1,[1]!Table,MATCH(A$1,[1]!Curves,0))</f><v>6.158</v></c><c r="C2"><f>YEAR("15-mar-99")</f><v>1999</v></c></row>'
  printf '<row r="3"><c r="B3"><f>Table+1</f><v>43</v></c><c r="C3"><f>DAY("5-Dec-2001")</f><v>5</v></c></row>'
  printf '<row r="4"><c r="C4"><f>MONTH("2001-03-15")</f><v>3</v></c></row>'
  printf '<row r="5"><c r="C5" t="e"><f>MONTH("BANANA-1-2")</f><v>#VALUE!</v></c></row>'
  printf '</sheetData></worksheet>'
} >"$edir2/xl/worksheets/sheet1.xml"
( cd "$edir2" && zip -q -r -X ../extd.xlsx . )
cat >"$tmp/extd.bas" <<'EOB'
program main(args)
  wb = xlsx.open(args[0])
  print "internal name works    : " + xlsx.evaluate(wb, "X", "B3")
  print "external name refused  : " + xlsx.evaluate(wb, "X", "B1")
  print "month-name date        : " + xlsx.evaluate(wb, "X", "C1")
  print "lowercase, 2-digit year: " + xlsx.evaluate(wb, "X", "C2")
  print "one-digit day          : " + xlsx.evaluate(wb, "X", "C3")
  print "numeric form still ok  : " + xlsx.evaluate(wb, "X", "C4")
  print "junk still refuses     : " + xlsx.evaluate(wb, "X", "C5")
  r = xlsx.check(wb, "X")
  print "check disagree         : " + string(r.disagree)
  print "check unsupported      : " + string(r.unsupported)
end program
EOB
extd_want='internal name works    : 43
external name refused  : #REF!
month-name date        : 1
lowercase, 2-digit year: 1999
one-digit day          : 5
numeric form still ok  : 3
junk still refuses     : #VALUE!
check disagree         : 0
check unsupported      : 2'
extd_got=$(timeout 60 ./gbasic "$tmp/extd.bas" "$tmp/extd.xlsx" 2>&1)
if [ "$extd_got" = "$extd_want" ]; then
    printf 'PASS extd: external names never resolve internally, month names coerce\n'
else
    printf 'FAIL extd\n'
    diff <(printf '%s\n' "$extd_want") <(printf '%s\n' "$extd_got") || true
    status=1
fi

# --- Tier 2f: post-2001 capabilities --------------------------------------------
#
# The corpus is 2001 and cannot contain one function added since, so everything
# §13.I/J measures is about the durable core. This fixture covers the other half.
# Its cached values were computed by LIBREOFFICE (tools/make_xlsx_modern_fixture.sh),
# which makes xlsx.check here a comparison against an independent implementation
# rather than against our own output -- the weakness every hand-written fixture
# has. LibreOffice is not Excel, so this is strong evidence, not proof; the
# fixture's arithmetic is also small enough to check by eye.
printf -- '-- post-2001 functions (fixture computed by LibreOffice)\n'
if timeout 120 ./gbasic examples/xlsx_modern_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_modern_test.out "$out"; then
        printf 'PASS examples/xlsx_modern_test.bas\n'
    else
        printf 'FAIL examples/xlsx_modern_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_modern_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# ZERO disagreements against the other implementation, asserted separately from
# the golden -- a golden records whatever we produce, including a regression.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  r = xlsx.check(wb, xlsx.sheets(wb)[0])\n  print r.disagree\n  print r.unsupported\nend program\n' "$MODERN_FIXTURE" >"$tmp/mod.bas"
mod_res=$(timeout 60 ./gbasic "$tmp/mod.bas" 2>/dev/null | tr '\n' ' ')
if [ "$mod_res" = "0 0 " ]; then
    printf 'PASS every post-2001 formula agrees with LibreOffice, none unsupported\n'
else
    printf 'FAIL post-2001 check: disagree/unsupported were "%s", want "0 0"\n' "$mod_res"
    status=1
fi

# The future-function prefix must be STRIPPED for real functions and KEPT for
# add-ins and VBA. Stripping _xll. would turn Enron's _xll.HPVAL (9,240 uses in
# the corpus) into a call to a function named HPVAL that we would then report as
# merely unimplemented -- hiding the fact that it is unevaluable in principle.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  print xlsx.cell(wb, xlsx.sheets(wb)[0], "F19").formula\nend program\n' "$MODERN_FIXTURE" >"$tmp/pfx.bas"
if timeout 60 ./gbasic "$tmp/pfx.bas" 2>/dev/null | grep -q '^_xlfn\.CONCAT'; then
    printf 'PASS the prefix survives in the FORMULA TEXT (only evaluation strips it)\n'
else
    printf 'FAIL the stored formula text should keep its _xlfn. prefix\n'
    status=1
fi


# --- Tier 2f2: the TEXT and MATH families ---------------------------------------
#
# Added 2026-08-15, and the reason is a correction to how this roadmap was being
# read. `xlsx.check` counted `unsupported` cells but never recorded WHICH name it
# refused, so the only available ranking was counting `NAME(` tokens in formula
# text -- the method §13.J had already shown to be structurally blind. Once the
# notes carried `blocked_by` and the corpus was re-ranked by the name actually
# refused, the top was not the lookup/aggregate work that was next on the plan
# (~14k cells) but FIND at 240,587 blocked cells and LEFT at 207,757, with LN,
# EXP, SQRT, MID and HOUR behind them: ordinary functions never written.
#
# Fixture values are LibreOffice's, so this is a comparison against an
# independent implementation. Deliberately NOT covered by it: CEILING/FLOOR on a
# negative, because LibreOffice exports those as _xlfn.CEILING.MATH and then
# cannot evaluate its own output (it caches #VALUE!), so it is no oracle for
# them -- that behaviour follows Microsoft's documentation and has no second
# opinion behind it, which the generator says outright.
printf -- '-- text and math families (fixture computed by LibreOffice)\n'
if timeout 120 ./gbasic examples/xlsx_textmath_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_textmath_test.out "$out"; then
        printf 'PASS examples/xlsx_textmath_test.bas\n'
    else
        printf 'FAIL examples/xlsx_textmath_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_textmath_test.bas (exit)\n'
    cat "$err"
    status=1
fi

TEXTMATH_FIXTURE=examples/fixtures/xlsx/textmath.xlsx
printf 'program main(args)\n  wb = xlsx.open("%s")\n  r = xlsx.check(wb, xlsx.sheets(wb)[0])\n  print r.disagree\n  print r.unsupported\nend program\n' "$TEXTMATH_FIXTURE" >"$tmp/tm.bas"
tm_res=$(timeout 60 ./gbasic "$tmp/tm.bas" 2>/dev/null | tr '\n' ' ')
if [ "$tm_res" = "0 0 " ]; then
    printf 'PASS every text/math formula agrees with LibreOffice, none unsupported\n'
else
    printf 'FAIL text/math check: disagree/unsupported were "%s", want "0 0"\n' "$tm_res"
    status=1
fi

# The distinctions a plausible-but-wrong implementation gets wrong. Asserted by
# name so a regression says WHICH rule broke, rather than only that a golden
# moved. Each pair differs precisely where a naive implementation would not.
rule() { # label expected actual
    if [ "$2" = "$3" ]; then
        printf 'PASS rule %-34s %s\n' "$1" "$2"
    else
        printf 'FAIL rule %-34s expected %s, got %s\n' "$1" "$2" "$3"
        status=1
    fi
}
# Read straight out of the golden by label, which keeps this tier honest about
# using the same computed values the fixture check just validated.
tm_label() { grep -F "  $1 = " examples/xlsx_textmath_test.out | head -1 | sed "s/^  $1 = //"; }
rule "INT floors a negative"          "-2"          "$(tm_label 'INT negative')"
rule "TRUNC truncates a negative"     "-1"          "$(tm_label 'TRUNC negative')"
rule "MOD takes the divisor sign"     "1"           "$(tm_label 'MOD negative')"
rule "MOD neg divisor"                "-1"          "$(tm_label 'MOD neg divisor')"
rule "FIND is case-sensitive"         "not-found"   "$(tm_label 'FIND case')"
rule "SEARCH is not"                  "1"           "$(tm_label 'SEARCH case')"
rule "FIND absent is an error"        "not-found"   "$(tm_label 'FIND absent')"
rule "TRIM collapses interior runs"   "Loan A East" "$(tm_label 'TRIM interior')"
rule "MID start<1 is an error"        "err"         "$(tm_label 'MID start err')"
rule "CEILING rounds to a multiple"   "2.5"         "$(tm_label 'CEILING')"
rule "time parts are self-consistent" "33"          "$(tm_label 'MINUTE')"

# --- Tier 2g: cross-sheet references --------------------------------------------
#
# The largest single cause of disagreement in the corpus: 3.09M cells, 54%
# (§13.J). Fixture values computed by LibreOffice, like modern.xlsx.
printf -- '-- cross-sheet references and the lookup family\n'
if timeout 120 ./gbasic examples/xlsx_crosssheet_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_crosssheet_test.out "$out"; then
        printf 'PASS examples/xlsx_crosssheet_test.bas\n'
    else
        printf 'FAIL examples/xlsx_crosssheet_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_crosssheet_test.bas (exit)\n'
    cat "$err"
    status=1
fi

printf 'program main(args)\n  wb = xlsx.open("%s")\n  r = xlsx.check(wb, "Calc")\n  print r.disagree\n  print r.unsupported\nend program\n' "$CROSS_FIXTURE" >"$tmp/cs.bas"
cs_res=$(timeout 60 ./gbasic "$tmp/cs.bas" 2>/dev/null | tr '\n' ' ')
if [ "$cs_res" = "0 0 " ]; then
    printf 'PASS every cross-sheet formula agrees with LibreOffice\n'
else
    printf 'FAIL cross-sheet check: disagree/unsupported were "%s", want "0 0"\n' "$cs_res"
    status=1
fi

# An EXTERNAL workbook reference must be reported as unavailable, not guessed
# at from Excel's stale cached copy and not silently zeroed. 28% of the corpus
# cross-sheet population is this shape.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  print xlsx.evaluate(wb, "Calc", "A2")\nend program\n' "$CROSS_FIXTURE" >"$tmp/ex.bas"
timeout 60 ./gbasic "$tmp/ex.bas" >/dev/null 2>&1 && printf 'PASS a resolvable cross-sheet ref still evaluates\n' || { printf 'FAIL cross-sheet evaluation broke\n'; status=1; }


# --- Tier 2h: workbook-wide recalculation ---------------------------------------
#
# Ordering ACROSS sheets. The per-sheet form became a trap once cross-sheet
# references worked: a formula can depend on a FORMULA on another sheet, and
# recalculating only its own sheet reads the other's stale cached value.
printf -- '-- workbook-wide recalc (ordering across sheets)\n'
if timeout 120 ./gbasic examples/xlsx_workbook_recalc_test.bas >"$out" 2>"$err" </dev/null; then
    if diff -u examples/xlsx_workbook_recalc_test.out "$out"; then
        printf 'PASS examples/xlsx_workbook_recalc_test.bas\n'
    else
        printf 'FAIL examples/xlsx_workbook_recalc_test.bas (output differs)\n'
        status=1
    fi
else
    printf 'FAIL examples/xlsx_workbook_recalc_test.bas (exit)\n'
    cat "$err"
    status=1
fi

# THE ORDERING, named rather than implied. Sheets are declared Out, Mid, Inputs
# -- the reverse of the dependency order -- so an engine recalculating in sheet
# order hands Out a stale Mid and prints 21. The only correct answer is 201.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  xlsx.set(wb, "Inputs", "A1", 100)\n  xlsx.recalc(wb)\n  print xlsx.cell(wb, "Out", "A1").value\n  print xlsx.cell(wb, "Mid", "A1").value\nend program\n' "$CHAIN_FIXTURE" >"$tmp/wb.bas"
wb_res=$(timeout 60 ./gbasic "$tmp/wb.bas" 2>/dev/null | tr '\n' ' ')
if [ "$wb_res" = "201 200 " ]; then
    printf 'PASS transitive cross-sheet dependent recomputed in the right order\n'
else
    printf 'FAIL cross-sheet ordering: got "%s", want "201 200 " (21 means a stale read)\n' "$wb_res"
    status=1
fi

# A cycle spanning two sheets is reported, and a healthy cell beside it still
# evaluates -- one cycle must not sink the workbook.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  r = xlsx.recalc(wb)\n  print r.circular\n  print xlsx.cell(wb, "Cycle", "B1").value\nend program\n' "$CHAIN_FIXTURE" >"$tmp/cyc.bas"
cyc_res=$(timeout 60 ./gbasic "$tmp/cyc.bas" 2>/dev/null | tr '\n' ' ')
if [ "$cyc_res" = "2 30 " ]; then
    printf 'PASS cross-sheet cycle reported; healthy neighbour still evaluates\n'
else
    printf 'FAIL cross-sheet cycle: got "%s", want "2 30 "\n' "$cyc_res"
    status=1
fi


# --- Tier 2i: an INDEPENDENT reader opens what we write --------------------------
#
# The gap this closes: everything else asserting our output is well-formed is
# either our own reader or `unzip -t`, and unzip only proves it is a valid ZIP,
# not a valid workbook. Nothing had ever confirmed that another spreadsheet
# implementation can open a file we wrote.
#
# LibreOffice is not Excel, so this is strong evidence and not proof -- the same
# standing caveat as the fixtures it generates. But it exercises the whole read
# path of a foreign implementation: shared strings, an XML entity, non-ASCII, a
# sparse row, a number format, a boolean, an error value and a formula's cached
# value all have to survive for the CSV below to come out right.
#
# SKIPs when libreoffice is absent, per the optional-dependency convention --
# the suite must not require it.
if command -v libreoffice >/dev/null 2>&1; then
    printf -- '-- an independent implementation opens what we wrote\n'
    printf 'program main(args)\n  wb = xlsx.open("%s")\n  xlsx.set(wb, "Ledger", "B2", 4242.42)\n  xlsx.save(wb, args[0])\nend program\n' "$FIXTURE" >"$tmp/wr.bas"
    if timeout 60 ./gbasic "$tmp/wr.bas" "$tmp/ours.xlsx" >/dev/null 2>"$err" </dev/null &&
       timeout 240 libreoffice --headless --convert-to csv "$tmp/ours.xlsx" --outdir "$tmp" >/dev/null 2>&1 &&
       [ -f "$tmp/ours.csv" ]; then
        miss=""
        # The value we wrote, and the shapes most likely to be lost by a writer
        # that produced a merely-valid ZIP: an entity, non-ASCII, an error, a
        # boolean, a styled date and a formula's cached value.
        grep -q '4242.42'          "$tmp/ours.csv" || miss="$miss written-value"
        grep -q 'Opening & carry'  "$tmp/ours.csv" || miss="$miss xml-entity"
        grep -q 'café'             "$tmp/ours.csv" || miss="$miss utf8"
        grep -q '#DIV/0!'          "$tmp/ours.csv" || miss="$miss error-value"
        grep -q 'TRUE'             "$tmp/ours.csv" || miss="$miss boolean"
        grep -q '2023'             "$tmp/ours.csv" || miss="$miss date-format"
        grep -q '2302.5'           "$tmp/ours.csv" || miss="$miss formula-cached-value"
        if [ -z "$miss" ]; then
            printf 'PASS LibreOffice opened our written workbook and read every shape back\n'
        else
            printf 'FAIL LibreOffice read our file but lost:%s\n' "$miss"
            cat "$tmp/ours.csv"
            status=1
        fi
    else
        printf 'FAIL LibreOffice could not open a workbook we wrote\n'
        cat "$err"
        status=1
    fi
else
    printf 'SKIP independent-reader tier (libreoffice not installed)\n'
fi

# --- Tier 3: negative ----------------------------------------------------------
printf -- '-- negative (container errors are reported, not guessed at)\n'
negative() { # label file expected-substring
    local label=$1 file=$2 want=$3
    printf 'program main(args)\n  print xlsx.open("%s")\nend program\n' "$file" >"$tmp/neg.bas"
    if timeout 60 ./gbasic "$tmp/neg.bas" >/dev/null 2>"$err" </dev/null; then
        printf 'FAIL negative %-18s (expected a raise, succeeded)\n' "$label"
        status=1
        return
    fi
    if grep -qF "$want" "$err"; then
        printf 'PASS negative %-18s %s\n' "$label" "$want"
    else
        printf 'FAIL negative %-18s\n  want: %s\n  got:  %s\n' "$label" "$want" "$(cat "$err")"
        status=1
    fi
}

printf 'this is not a zip file at all, not even close\n' >"$tmp/notzip.xlsx"
negative "not a container" "$tmp/notzip.xlsx" "not a ZIP container"

head -c 400 "$FIXTURE" >"$tmp/trunc.xlsx"
negative "truncated" "$tmp/trunc.xlsx" "xlsx"

negative "missing file" "$tmp/does_not_exist.xlsx" "cannot read"

# Writing over a formula cell must RAISE. The cached value and the formula would
# disagree, Excel would recalculate on open, and the edit would silently revert
# -- a wrong answer that looks like a successful write.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  xlsx.set(wb, "Ledger", "B5", 1)\nend program\n' "$FIXTURE" >"$tmp/neg2.bas"
if timeout 60 ./gbasic "$tmp/neg2.bas" >/dev/null 2>"$err" </dev/null; then
    printf 'FAIL negative %-18s (overwrote a formula cell)\n' "formula overwrite"
    status=1
elif grep -qF "refusing to overwrite a formula cell" "$err"; then
    printf 'PASS negative %-18s refusing to overwrite a formula cell\n' "formula overwrite"
else
    printf 'FAIL negative %-18s wrong message: %s\n' "formula overwrite" "$(cat "$err")"
    status=1
fi

# Creating a cell that does not exist is refused rather than placed wrongly.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  xlsx.set(wb, "Ledger", "Z99", 1)\nend program\n' "$FIXTURE" >"$tmp/neg3.bas"
if timeout 60 ./gbasic "$tmp/neg3.bas" >/dev/null 2>"$err" </dev/null; then
    printf 'FAIL negative %-18s (invented a cell)\n' "new cell"
    status=1
elif grep -qF "creating a new cell is not supported" "$err"; then
    printf 'PASS negative %-18s new-cell creation refused explicitly\n' "new cell"
else
    printf 'FAIL negative %-18s wrong message: %s\n' "new cell" "$(cat "$err")"
    status=1
fi

# A partless sheet READS as empty, but a name that is not in the workbook at all
# must still raise -- otherwise the leniency above is blanket leniency and a
# typo'd sheet name silently returns nothing.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  print count(xlsx.cells(wb, "NoSuchSheet"))\nend program\n' "$MACRO_FIXTURE" >"$tmp/neg4.bas"
if timeout 60 ./gbasic "$tmp/neg4.bas" >/dev/null 2>"$err" </dev/null; then
    printf 'FAIL negative %-18s (an absent sheet returned a result)\n' "absent sheet"
    status=1
elif grep -qF "no such sheet: NoSuchSheet" "$err"; then
    printf 'PASS negative %-18s absent sheet still raises\n' "absent sheet"
else
    printf 'FAIL negative %-18s wrong message: %s\n' "absent sheet" "$(cat "$err")"
    status=1
fi

# Writing to a partless sheet has nowhere to put the cell, so it is refused --
# but with the REASON, not by claiming the sheet does not exist. The old message
# was a false statement, which is what sent the corpus scan looking in the wrong
# place; pinning the wording is the point of this case.
printf 'program main(args)\n  wb = xlsx.open("%s")\n  xlsx.set(wb, "Module1", "A1", 1)\nend program\n' "$MACRO_FIXTURE" >"$tmp/neg5.bas"
if timeout 60 ./gbasic "$tmp/neg5.bas" >/dev/null 2>"$err" </dev/null; then
    printf 'FAIL negative %-18s (wrote to a sheet with no part)\n' "write partless"
    status=1
elif grep -qF "has no worksheet part" "$err" && ! grep -qF "no such sheet" "$err"; then
    printf 'PASS negative %-18s refused with the real reason, not "no such sheet"\n' "write partless"
else
    printf 'FAIL negative %-18s wrong message: %s\n' "write partless" "$(cat "$err")"
    status=1
fi

# --- Tier 4: valgrind ----------------------------------------------------------
if command -v valgrind >/dev/null 2>&1; then
    if valgrind --error-exitcode=9 --leak-check=full --errors-for-leak-kinds=definite \
            ./gbasic examples/xlsx_read_test.bas >"$out" 2>"$err" </dev/null; then
        if diff -q examples/xlsx_read_test.out "$out" >/dev/null; then
            printf 'PASS valgrind examples/xlsx_read_test.bas\n'
        else
            printf 'FAIL valgrind (output differs under valgrind)\n'
            status=1
        fi
    else
        printf 'FAIL valgrind examples/xlsx_read_test.bas\n'
        tail -25 "$err"
        status=1
    fi
else
    printf 'SKIP valgrind (not installed)\n'
fi

exit "$status"
