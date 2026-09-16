#!/usr/bin/env bash
# BAI2 -- the THIRD adapter and the third structural shape
# (docs/financial_adapters_design.md §20).
#
# NACHA is fixed-width, camt is hierarchical XML, and this is DELIMITED AND
# VARIABLE-LENGTH with a logical record that can span several physical ones. It
# is the first caller of §4's `delimited` location kind, which had existed in
# the enum with nothing using it.
#
# THE FOREIGN CORPUS WAS READ BEFORE A LINE OF THE ADAPTER WAS WRITTEN. That is
# the correction from the NACHA work, where real files arrived last and found
# three defects that had already shipped -- and it is why the framing here is
# right rather than retrofitted. Four facts came out of that reading, none of
# which a fixture written here would have contained: the record separator is a
# SLASH rather than a newline (one real file packs two whole records onto a
# line); a record whose last field is free text often has NO terminator at all
# (102 of 116 records in one sample); that text CONTAINS SLASHES, which
# shatters a reader splitting on each one; and an `88` continues the previous
# record's FIELD LIST rather than its text, which is the defining feature of
# the format and the thing this adapter got wrong first.
#
# THE LOAD-BEARING TIER IS `framings`, inside the fixture: the same logical
# file written three physically different ways must give one answer. Asserting
# any single form passes on a reader that handles only that form.
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

printf 'TIER semantics\n'
out="$scratch/b.out"
if timeout 180 ./gbasic tests/finio/bai2_test.bas >"$out" 2>&1; then
    mism="$(sed -n 's/^mismatches: //p' "$out")"
    checks="$(sed -n 's/^checks: //p' "$out")"
    if [ "$mism" = "0" ] && [ "${checks:-0}" -ge 38 ]; then
        ok "$checks checks, 0 mismatches"
    else
        bad "bai2_test: $checks checks, $mism mismatches"
        grep MISMATCH "$out" || true
    fi
else
    bad "bai2_test exited nonzero"; cat "$out"
fi

# --- EXTERNAL ORACLE -------------------------------------------------------
# Python recomputes the three-level control totals from the bytes. It has seen
# neither this adapter nor the generator, which matters because both were
# written here and would agree with each other however wrong they were.
printf 'TIER oracle\n'
if ! command -v python3 >/dev/null 2>&1; then
    printf '  SKIP oracle (no python3)\n'
else
cat > "$scratch/oracle.py" <<'PYEOF'
import sys
def fields(rec):
    return rec.rstrip('/').split(',')
def extra(f, i):
    if i >= len(f): return 0
    v = f[i].strip()
    if v == 'S': return 3
    if v == 'V': return 2
    if v == 'D':
        try: return 1 + int(f[i+1]) * 2
        except: return 1
    return 0
def num(s):
    s = s.strip()
    if not s: return None
    neg = s.startswith('-')
    if s[0] in '+-': s = s[1:]
    if not s.isdigit(): return None
    return -int(s) if neg else int(s)

recs = []
for line in open(sys.argv[1]):
    line = line.rstrip('\n').rstrip('\r')
    # records are terminated by / or by end of line; a fragment whose first
    # field is not a record code belongs to the previous record.
    for piece in line.split('/'):
        p = piece.strip()
        if not p: continue
        code = p.split(',')[0].strip()
        if recs and code not in ('01','02','03','16','49','88','98','99'):
            recs[-1] += '/' + p
        else:
            recs.append(p)
# merge continuations into the record they continue
merged, owner = [], []
for r in recs:
    if r.split(',')[0].strip() == '88' and merged:
        merged[-1] += ',' + ','.join(fields(r)[1:])
        owner.append(len(merged) - 1)
    else:
        merged.append(r); owner.append(len(merged) - 1)

total_file, groups, out = 0, 0, []
acct = None
for r in merged:
    f = fields(r); code = f[0].strip()
    if code == '02': groups += 1; gtot = 0; naccts = 0
    if code == '03':
        acct = 0; naccts += 1
        j = 3
        while j < len(f):
            if f[j].strip():
                v = num(f[j+1]) if j+1 < len(f) else None
                if v is not None: acct += v
            j += 4 + extra(f, j+3)
    if code == '16' and acct is not None:
        v = num(f[2]) if len(f) > 2 else None
        if v is not None: acct += v
    if code == '49' and acct is not None:
        out.append("account %d" % acct); gtot += acct; acct = None
    if code == '98':
        out.append("group %d accounts %d" % (gtot, naccts)); total_file += gtot
print("\n".join(out))
print("file %d groups %d" % (total_file, groups))
PYEOF
for f in tests/finio/bai2/statement.bai tests/finio/bai2/packed.bai tests/finio/bai2/continued.bai tests/finio/foreign_bai2/spec-section3.txt; do
    py="$(python3 "$scratch/oracle.py" "$f" 2>&1 | tr '\n' ';')"
    gb="$(timeout 60 ./gbasic tests/finio/bai2_totals.bas "$f" 2>&1 | tr '\n' ';')"
    if [ -n "$py" ] && [ "$py" = "$gb" ]; then
        ok "$(basename "$f"): an independent implementation agrees [$py]"
    else
        bad "$(basename "$f") disagreement"
        printf '    python: %s\n    finio : %s\n' "$py" "$gb"
    fi
done
fi

# --- PROVENANCE AND RIGHTS -------------------------------------------------
printf 'TIER provenance\n'
man=tests/finio/foreign_bai2/PROVENANCE.txt
if [ ! -f "$man" ] || [ ! -f tests/finio/foreign_bai2/LICENSE-moov-io-bai2 ]; then
    bad "the foreign vectors are redistributed without a manifest or the licence beside them"
else
    miss=0; n=0
    for f in tests/finio/foreign_bai2/*.txt; do
        b="$(basename "$f")"
        [ "$b" = "PROVENANCE.txt" ] && continue
        n=$((n + 1))
        line="$(grep "^$b	" "$man" || true)"
        if [ -z "$line" ]; then bad "  $b appears nowhere in PROVENANCE.txt"; miss=$((miss + 1)); continue; fi
        want="$(printf '%s' "$line" | cut -f2)"
        got="$(sha256sum "$f" | cut -d' ' -f1)"
        [ "$want" = "$got" ] || { bad "  $b does not match its recorded hash"; miss=$((miss + 1)); }
    done
    if [ "$miss" = "0" ] && [ "$n" -ge 6 ]; then
        ok "all $n foreign vectors carry a source, a date, a licence and a matching hash"
    fi
    notok="$(awk -F'\t' '/^[^#]/ && NF>=7 && $7 != "yes" { print $1 }' "$man" | tr '\n' ' ')"
    [ -z "$notok" ] && ok "and every one permits redistribution" || bad "committed without redistribution rights: $notok"
    grep -q 'EXCLUDED' "$man" && ok "and what was NOT used says why" || bad "no exclusions recorded"
fi

printf 'TIER generator\n'
if command -v python3 >/dev/null 2>&1; then
    python3 tools/make_bai2_fixture.py "$scratch/regen" >/dev/null 2>&1
    if diff -r tests/finio/bai2 "$scratch/regen" >/dev/null 2>&1; then
        ok "the committed fixtures are byte-identical to what the generator writes"
    else
        bad "the committed fixtures have drifted from tools/make_bai2_fixture.py"
        diff -rq tests/finio/bai2 "$scratch/regen" || true
    fi
else
    printf '  SKIP generator drift (no python3)\n'
fi

printf 'TIER valgrind\n'
if vg_available; then
    if vg_run ./gbasic tests/finio/bai2_test.bas >/dev/null 2>&1; then
        ok "no definite leak or invalid access over the adapter"
    else
        bad "valgrind"
    fi
else
    printf '  SKIP valgrind (unavailable)\n'
fi

if [ "$fails" = "0" ]; then
    printf 'run_finio_bai2: all cases passed\n'
else
    printf 'run_finio_bai2: %d FAILED\n' "$fails"
    exit 1
fi
