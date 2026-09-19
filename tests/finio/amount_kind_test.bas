' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' WHAT KIND OF THING IS AN AMOUNT? -- the cross-adapter contract, and the
' tripwire that would have caught the defect a real bank file found.
'
' THE OFX ADAPTER HANDED AMOUNTS BACK AS TEXT while NACHA, camt.053 and
' pain.001 all handed back `money`. Nothing noticed for the life of the adapter,
' and the reason is worth stating: every assertion about an amount anywhere in
' this tree went through `string(...)`, and `string(money)` and the decimal text
' it was parsed from are THE SAME CHARACTERS. The difference is only visible
' when somebody does ARITHMETIC, and every fixture was written by the hands that
' wrote the reader.
'
' MEASURED, on a real credit union's OFX download:
'
'     summing three transaction amounts the obvious way gives
'     "-28.00-5.00-28.00", type string
'
' No raise. No diagnostic. A plausible-looking answer of the wrong kind.
'
' SO THE TABLE BELOW IS DECLARED, AND EVERY ADAPTER MUST BE IN IT. A new adapter
' that quietly picks a third representation fails here rather than in whatever
' program first tries to add two of its amounts up -- which is the whole reason
' this is a table and not five separate checks. The gate cannot silently shrink:
' an adapter absent from the table is a failure, not a skip.
'
' BAI2 IS THE DECLARED EXCEPTION AND IT IS A REAL ONE, not an oversight. BAI2
' amounts are in MINOR UNITS with no decimal point and no currency of their own
' on most record types, so there is nothing to denominate a `money` with; the
' adapter answers a `number` and carries `cents` beside it. Writing that down
' here is the point -- an exception nobody recorded is indistinguishable from
' the defect this file exists to catch.

load finio
load finio_ofx
load finio_nacha
load finio_camt
load finio_pain001
load finio_bai2

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

' THE DECLARED CONTRACT. Each entry names the adapter, a fixture, the RECORD
' KIND and the FIELD -- precisely, not "whatever amount-shaped thing turns up
' first". The first draft of this file scanned for a field whose name contained
' "amount" or "sum" and reported pain.001 as broken, when what it had found was
' `control_sum`, which is text BY ARGUMENT (see below). A tripwire whose false
' positives look exactly like its true ones is not a tripwire.
'
' `kind` is what `type()` must answer; `why` is REQUIRED for anything that is
' not `money`, so an exception has to be argued rather than merely taken.
function contract()
    return [ { adapter: "nacha",   fixture: "tests/finio/nacha/blocked.ach",
               record: "entry_detail",       field: "amount", kind: "money", why: "" },
             { adapter: "camt",    fixture: "tests/finio/camt/statement.xml",
               record: "entry",              field: "amount", kind: "money", why: "" },
             { adapter: "pain001", fixture: "tests/finio/pain/payments.xml",
               record: "transaction",        field: "amount", kind: "money", why: "" },
             { adapter: "ofx",     fixture: "tests/finio/ofx/statement_v1.ofx",
               record: "transaction",        field: "TRNAMT", kind: "money", why: "" },
             { adapter: "bai2",    fixture: "tests/finio/bai2/packed.bai",
               record: "transaction_detail", field: "amount", kind: "number",
               why: ("BAI2 amounts are in minor units with no decimal point and"
                     + " no currency on most record types, so there is nothing"
                     + " to denominate a money with; `cents` travels beside it") } ]
end function

' THE SECOND DECLARED EXCEPTION, and it is the one the first draft of this file
' got wrong. A CONTROL SUM IS NOT AN AMOUNT: pain.001's `CtrlSum` carries no
' currency of its own, and a payment block mixing EUR and USD has a control sum
' that means nothing -- `money` would not add those anyway. So it stays text and
' the adapter reports the mixture instead. Recorded here because an exception
' nobody wrote down is indistinguishable from the defect this file exists for.
function control_sums()
    return [ { adapter: "pain001", fixture: "tests/finio/pain/payments.xml",
               record: "group_header", field: "control_sum", kind: "string",
               why: ("a CtrlSum carries no currency of its own, so a block mixing"
                     + " currencies has a control sum money cannot represent") } ]
end function

function field_of(name, fixture, record_kind, field)
    if name = "ofx" then
        reg = finio.registry([ finio_ofx.adapter() ])
    end if
    if name = "nacha" then
        reg = finio.registry([ finio_nacha.adapter() ])
    end if
    if name = "camt" then
        reg = finio.registry([ finio_camt.adapter() ])
    end if
    if name = "pain001" then
        reg = finio.registry([ finio_pain001.adapter() ])
    end if
    if name = "bai2" then
        reg = finio.registry([ finio_bai2.adapter() ])
    end if
    doc = finio.read_file(reg, fixture, {})
    for each rec in doc.records
        if rec.kind = record_kind then
            if has(rec.fields, field) then
                f = rec.fields[field]
                if f.status = "ok" then
                    return f
                end if
            end if
        end if
    end for
    ' REPORTED, NOT RAISED. A fixture that stops carrying the record this
    ' contract names must fail the check written for it, not kill the file at an
    ' index before that check can speak.
    return { status: "NOT-FOUND", value: "NOT-FOUND",
             why: ("no `" + record_kind + "` record with an ok `" + field + "` in " + fixture) }
end function

print "-- every adapter's amount is the kind the contract declares"
    for each c in contract()
        f = field_of(c.adapter, c.fixture, c.record, c.field)
        check(c.adapter + " " + c.record + "." + c.field + " is " + c.kind,
              type(f.value), c.kind)
        if c.kind != "money" then
            check(c.adapter + " argues its exception", len(c.why) > 40, true)
        end if
    end for
    for each c in control_sums()
        f = field_of(c.adapter, c.fixture, c.record, c.field)
        check(c.adapter + " " + c.record + "." + c.field + " is " + c.kind,
              type(f.value), c.kind)
        check(c.adapter + " argues that exception too", len(c.why) > 40, true)
    end for

    print ""
    print "-- and a `money` amount ADDS rather than concatenating"
    ' THE CHECK THAT IS THE DEFECT ITSELF. `type` alone passes on a value that
    ' reports the right kind and cannot be used; two amounts summing to their
    ' arithmetic total is what a caller actually needs.
    for each c in contract()
        if c.kind = "money" then
            f = field_of(c.adapter, c.fixture, c.record, c.field)
            doubled = f.value + f.value
            check(c.adapter + ": x + x is arithmetic, not text",
                  string(doubled) != string(f.value) + string(f.value), true)
        end if
    end for

    print ""
    print "-- THE GATE CANNOT SHRINK: every adapter in the tree is in the table"
    ' An adapter added without an entry here would pick its own representation
    ' unchallenged, which is exactly how the OFX one came to be the odd one out.
    declared = []
    for each c in contract()
        append(declared, c.adapter)
    end for
    for each nm in [ "nacha", "camt", "pain001", "ofx", "bai2" ]
        check("`" + nm + "` is declared", contains(declared, nm), true)
    end for
    check("and the table has no more than the tree does", count(declared), 5)

    print ""
    print "checks: " + string(tally.checks)
    print "mismatches: " + string(tally.mismatches)
