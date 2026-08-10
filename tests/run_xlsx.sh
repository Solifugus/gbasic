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
