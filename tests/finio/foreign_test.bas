' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' FILES THIS PROJECT DID NOT WRITE.
'
' Every other fixture in tests/finio/ was generated here, from this project's
' model of the format -- so the arithmetic oracles, strong as they are, check
' our reader against our generator's SHARED understanding. A file written by a
' different implementation embodies somebody else's model, which is the only
' kind of disagreement that can find a misunderstanding we hold consistently.
'
' TEN FILES FROM moov-io/ach (Apache-2.0), AND THEY FOUND THREE THINGS:
'   - FOUR of the ten were refused outright by a strict 94-byte rule, because
'     their producer STRIPPED TRAILING BLANKS -- a file header at 75 bytes, a
'     file control at exactly 55. Nothing is lost; the missing tail is the
'     blank part. Our generator pads to 94 because it pads to 94.
'   - ONE carries NON-ASCII: 94 codepoints, 95 bytes, in a format the
'     specification says is ASCII.
'   - `block_count` was computed as a FLOOR where the field counts the blocks
'     the file occupies, which is a ceiling. A conforming file is a multiple of
'     ten records so the two agree, and every fixture here is conforming.
'
' THE LOAD-BEARING TIER IS `clean`, and it is a DIFFERENCE: two of these files
' validate with ZERO issues and the other eight do not. Asserting only that
' they all read passes on an adapter that reports nothing; asserting only that
' findings exist passes on one that reports everything.

load finio
load finio_nacha

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function slurp(path)
    f {file}= path
    return read(f)
end function

function codes_of(reg, doc)
    out = []
    for each i in finio.validate(reg, doc)
        if not contains(out, i.code) then
            append(out, i.code)
        end if
    end for
    return out
end function

reg = finio.registry([ finio_nacha.adapter() ])
dir = "tests/finio/foreign/"
names = [ "ppd-debit.ach", "extended-ascii.ach", "cor-example.ach", "adv.ach",
          "gl-debit.ach", "20180713-IAT.ach", "NACHA_SAMPLE_TEL_REVERSAL.ach",
          "20110805A.ach", "FISERV-ZEROFILE.ach", "20110729A-invalid.ach" ]

print "-- EVERY FOREIGN FILE IS RECOGNISED AND READ"
' Four of these were `unknown` before the corpus arrived. A reader that refuses
' a file destroys the only thing an operator can work from (§15).
unrecognised = 0
unread = 0
clean = 0
for each nm in names
    t = slurp(dir + nm)
    if finio_nacha.recognise(t).classification = "unknown" then
        unrecognised = unrecognised + 1
    end if
    on error goto next
    d = finio.read_text(reg, t, {})
    if error then
        unread = unread + 1
        error.clear()
        on error stop
    else
        on error stop
        if count(finio.validate(reg, d)) = 0 then
            clean = clean + 1
        end if
    end if
end for
check("all ten are recognised", unrecognised, 0)
check("all ten read", unread, 0)

print ""
print "-- AND THE DIFFERENCE: SOME VALIDATE CLEAN AND MOST DO NOT"
' Without both halves this file passes on an adapter that reports nothing and
' on one that reports everything.
check("two validate with no issues at all", clean, 2)
check("and the other eight do not", count(names) - clean, 8)

print ""
print "-- WHAT EACH ONE FOUND, NAMED"
gl = finio.read_text(reg, slurp(dir + "gl-debit.ach"), {})
check("a general-ledger file from another implementation is clean",
      count(finio.validate(reg, gl)), 0)
check("and its arithmetic is OUR reading of THEIR file",
      count(gl.entities.batches), 1)

ppd = finio.read_text(reg, slurp(dir + "ppd-debit.ach"), {})
pc = codes_of(reg, ppd)
check("the trailing-blank file reports only its widths", join(pc, ","), "record_length")
check("it is `strong`, not `exact` -- readable and not conforming",
      ppd.classification, "strong")
check("its counts, hash and totals all reconcile", contains(pc, "batch_entry_hash"), false)
' AXIOM 7 ON A TRUNCATED RECORD: a field beyond the end is `unknown` -- the
' source said nothing -- and is never blank and never zero. Padding the record
' would turn "absent" into "blank", which is the distinction Axiom 7 exists for.
fh = ppd.records[0]
check("the short file header is 75 bytes", fh.byte_length, 75)
check("and a field past its end is unknown, not blank",
      fh.fields.reference_code.status, "unknown")
check("and unknown, never \"\"", is_unknown(fh.fields.reference_code.value), true)
check("while a field inside it reads normally", fh.fields.immediate_origin.status, "ok")

ea = finio.read_text(reg, slurp(dir + "extended-ascii.ach"), {})
check("the non-ASCII file reports exactly that", join(codes_of(reg, ea), ","), "non_ascii")
check("and the record really is 94 codepoints and more bytes",
      len(ea.records[14].raw) = 94 and ea.records[14].byte_length > 94, true)

advd = finio.read_text(reg, slurp(dir + "adv.ach"), {})
ac = codes_of(reg, advd)
check("an ADV batch is named as unsupported rather than silently mis-read",
      contains(ac, "unsupported_batch_kind"), true)
check("and the totals it produces are reported as disagreeing",
      contains(ac, "batch_credit_total"), true)

big = finio.read_text(reg, slurp(dir + "20110805A.ach"), {})
bc = codes_of(reg, big)
check("a file declaring five batches and holding four is caught",
      contains(bc, "file_batch_count"), true)
' THE BLOCK COUNT IS A CEILING. 93 records occupy 10 blocks and the file says
' 10; computed as a floor it said 9 and reported a disagreement that was ours.
check("and its declared block count is NOT reported as wrong",
      contains(bc, "block_count"), false)
check("while the missing padding still is", contains(bc, "blocking"), true)

cor = finio.read_text(reg, slurp(dir + "cor-example.ach"), {})
check("a transaction code not in the table is REPORTED, not guessed",
      contains(codes_of(reg, cor), "unknown_transaction_code"), true)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
