# Financial file formats cookbook

Worked recipes for `finio` — reading, validating and writing the file formats
the financial industry actually moves money with.

**This page cannot lie.** Every code block below is a real file under
`examples/finio_cookbook/`, and every output block is that file's actual
stdout, compared byte for byte by `tests/run_finio_cookbook.sh`.

**New to `finio`?** Start with the [tutorial](finio_tutorial.md), which works one
continuous problem — a counterparty's overnight drop directory — end to end.
This page is the task-oriented companion.

Design and rationale:
[financial_adapters_design.md](financial_adapters_design.md). Per-function
reference: [reference.md](reference.md#finio).

Run any recipe yourself:

```
GBASIC_PATH=stdlib ./gbasic examples/finio_cookbook/01_reading_a_file.bas
```

---

## What `finio` is

Five adapters ship today, across four structural shapes:

| adapter | format | shape |
|---|---|---|
| `finio_nacha` | ACH (NACHA) | fixed-width, 94 bytes |
| `finio_bai2` | BAI2 cash management | delimited, with continuations |
| `finio_camt` | ISO 20022 camt.053 statement | hierarchical XML |
| `finio_ofx` | OFX bank download | tagged, SGML 1.x and XML 2.x |
| `finio_pain001` | ISO 20022 pain.001 credit transfer | hierarchical XML, **outbound** |

Above the framework they are one shape: a document with records, entities and a
loss report. The adapters are not a collection of parsers with a shared
namespace — they are one value model, and the recipes below hardly ever name a
format.

### The axioms that show up in every recipe

- **Axiom 2 — provenance.** Every interpreted value traces to the bytes it came
  from. Recipe 2.
- **Axiom 6 — never silently guess.** A file nothing recognises is refused,
  naming what the registry holds; one that *two* adapters claim is refused too,
  rather than resolved by preference. Recipe 1 shows the first. The second needs
  two adapters claiming one format, so it lives in `tests/finio/nacha_test.bas`
  where a rival adapter exists for it — ambiguity is a property of a registry,
  and with one adapter registered it cannot arise.
- **Axiom 7 — `unknown` is not `invalid`.** A blank field and a broken field
  are different answers, and neither is zero. Recipe 3.
- **Axiom 8 — loss is explicit.** What was not interpreted is reported; the
  bytes are kept. Recipe 7.
- **§15 — reading and validating are different operations.** Invalid input is
  not necessarily unreadable input. Recipe 4.
- **§16 — writing refuses by default.** Lossy output may be permitted;
  impossible output may not. Recipe 8.

### A note on the fixtures

These recipes read the files under `tests/finio/`, which are the same files the
adapter suites assert against. That is deliberate rather than convenient: a
second copy under `examples/` would be a second thing that can drift, and
sharing them means the numbers on this page are the numbers the validation
checks. If a fixture changes, this page's goldens move and the suite says so.

---

## 1. Reading a file without knowing what it is

The ordinary way to read a financial file is to already know the format and
call the parser for it. That works right up until the file is not what the
filename said, which in this industry is often — a `.txt` off an SFTP drop
could be BAI2, could be a NACHA return, could be a bank's own dialect of
something.

So `finio` splits the question in two. `identify` reports what the evidence
says, **with the evidence**. `read_file` then reads it.

A file nothing recognises is refused. So is one that two adapters both claim —
Axiom 6, a plausible reading is never chosen silently.

The refusal at the end is the other half. Guessing would hand back an
ordinary-looking document read under the wrong layout — every field present,
every field wrong, and nothing raised.

Note the line above it. The adapter says it could not settle the
revision and names the default it fell back to. NACHA's record layout has
outlived twenty rule books and the file simply does not say which year's rules
produced it; inventing a year would put a false claim into every document
written from it.

<!--CODE:01_reading_a_file-->

```basic
' Recipe 1 — Reading a file without knowing what it is.
'
' The ordinary way to read a financial file is to already know the format and
' call the parser for it. That works right up until the file is not what the
' filename said, which in this industry is often — a `.txt` off an SFTP drop
' could be BAI2, could be a NACHA return, could be a bank's own dialect of
' something.
'
' So `finio` splits the question in two. `identify` reports what the evidence
' says, WITH the evidence. `read_file` then reads it. A file nothing recognises
' is refused, and so is one that two adapters both claim — a plausible reading
' is never chosen silently.
program main()
    load finio
    load finio_all

    reg = finio_all.registry()
    print ("registered: " + join(finio_all.ids(), ", "))
    print ""

    f {file}= "tests/finio/nacha/payroll.ach"
    text = read(f)

    ' What does the evidence say? `exact` is the strongest of finio's four
    ' classifications: exact, strong, possible, unknown.
    id = finio.identify(reg, text)
    print ("classification: " + id.classification)
    for each c in id.candidates
        print ("  " + c.adapter)
        for each why in c.reasons
            print ("    - " + why)
        end for
    end for
    print ""

    ' Now read it. Nothing here names the format.
    doc = finio.read_file(reg, "tests/finio/nacha/payroll.ach", {})
    print ("read as " + doc.adapter + ", " + string(count(doc.records)) + " records")
    print ("the document says WHY it read it that way:")
    for each why in doc.reasons
        print ("  - " + why)
    end for
    print ""

    ' And a file nothing recognises is REFUSED, naming what the registry holds
    ' and how to pin one. Guessing would hand back an ordinary-looking document
    ' read under the wrong layout — every field present, every field wrong.
    on error goto next
    doc = finio.read_file(reg, "tests/finio/nacha/not_ach.txt", {})
    if error then
        print "not_ach.txt:"
        print ("  " + error.message)
        error.clear()
    else
        print "not_ach.txt was read as something (this would be a bug)"
    end if
    on error stop
end program
```

<!--OUT:01_reading_a_file-->

```
registered: aba.nacha, iso20022.camt053, bai2, ofx, iso20022.pain001

classification: exact
  aba.nacha
    - 140 records, every one 94 bytes
    - the record-type sequence is well formed
    - the file header carries record size 094, blocking factor 10, format code 1

read as aba.nacha, 140 records
the document says WHY it read it that way:
  - recognised as aba.nacha (exact)
  - 140 records, every one 94 bytes
  - the record-type sequence is well formed
  - the file header carries record size 094, blocking factor 10, format code 1
  - the source carries no revision evidence; read under 'unresolved', the adapter's default

not_ach.txt:
  finio.read_text: no registered adapter recognised this source -- the registry holds aba.nacha, iso20022.camt053, bai2, ofx, iso20022.pain001. Pin one with { adapter: ... } if you know what it is.
```

---

## 2. Where a value came from

When a payment is wrong at four in the afternoon, the question is *which
characters of which record said so*. An answer of "row 137, somewhere" costs an
hour.

Each field carries `raw` — exactly the bytes, padding and all — and `value`,
what they were interpreted as. The **location** is not carried. It is asked for,
with `finio.source_value`, and computed from the retained source.

That is an architecture, not an omission, and it was decided by measurement
rather than preference. At the design's own scale — 100,000 records, eleven
fields, 1.1 million source locations — holding a location per value costs
**2.79 GB for a 9.5 MB file**, 294x, and is *also the slowest to answer*,
because the working set stops fitting in cache. Reconstructing on demand costs
about two microseconds a query. A consumer would need roughly two million
provenance queries before the per-value build alone paid for itself.

The last three lines of the recipe are the point: slice the original file at the
range the location names and the same bytes come back. The claim is checkable,
which is why the source is retained.

<!--CODE:02_where_a_value_came_from-->

```basic
' Recipe 2 — Where a value came from.
'
' Axiom 2: every interpreted value is traceable to the bytes it was read from.
' Not "the file it came from" — the BYTES. When a payment is wrong at four in
' the afternoon, the question is which characters of which record said so, and
' an answer of "row 137, somewhere" costs an hour.
'
' Each field carries `raw` (exactly the bytes, padding and all) and `value`
' (what they were interpreted as). The LOCATION is not carried — it is asked
' for, with `finio.source_value`, and computed from the retained source.
'
' That is an architecture rather than an omission, and it was measured rather
' than preferred: holding a location per value costs 294x the size of the file
' and is ALSO the slowest to answer, because the working set stops fitting in
' cache. Reconstructing costs about two microseconds a query.
program main()
    load finio
    load finio_all
    load finio_nacha

    reg = finio_all.registry()
    doc = finio.read_file(reg, "tests/finio/nacha/payroll.ach", {})

    r = doc.records[2]
    print ("record " + string(r.index) + " is a " + r.kind
           + ", " + string(r.byte_length) + " bytes at offset " + string(r.byte_offset))
    print ""

    ' The layout is the adapter's; asking for a location means saying which
    ' layout the record was read under.
    lay = finio_nacha.layout_for(r.kind)
    for each concept in [ "individual_name", "amount", "trace_number" ]
        fld = r.fields[concept]
        sv = finio.source_value(doc.source, lay, r.index, concept)
        print (concept + ":")
        print ("  value  " + string(fld.value))
        print ("  raw    [" + fld.raw + "]")
        print ("  where  " + finio.describe_location(sv.location))
    end for
    print ""

    ' The claim is checkable, which is the point of retaining the source: slice
    ' the original text at the offset the location names and the same bytes
    ' come back.
    text = finio.source_text(doc.source)
    sv = finio.source_value(doc.source, lay, r.index, "amount")
    cut = byte_slice(text, sv.location.byte_offset, sv.location.byte_length)
    print ("slicing the whole file at that range gives [" + cut + "]")
    print ("which is the bytes the field reported: " + string(cut = r.fields.amount.raw))
end program
```

<!--OUT:02_where_a_value_came_from-->

```
record 2 is a entry_detail, 94 bytes at offset 190

individual_name:
  value  ALICE MERCER
  raw    [ALICE MERCER          ]
  where  record 2, bytes 244..265
amount:
  value  1250.00
  raw    [0000125000]
  where  record 2, bytes 219..228
trace_number:
  value  021000021000001
  raw    [021000021000001]
  where  record 2, bytes 269..283

slicing the whole file at that range gives [0000125000]
which is the bytes the field reported: true
```

---

## 3. A blank field and a broken field are different answers

This is Axiom 7, and it is the recipe most likely to prevent a real defect.

A field that is **blank** means the source said nothing: `unknown`. A field that
is **present and fails its own rule** means the source said something that
cannot be what it claims: `invalid`. Collapsing them loses the distinction the
person reading the report needs — one is a gap to go and fill, the other is a
defect to send back.

The direction that actually hurts is reading a broken amount as **zero**. It
understates the file, and an understated file looks exactly like a small one,
with nothing raised anywhere. So `value` is `unknown` in both cases — never 0,
never `""` — and the raw bytes survive either way so a human can see what was
really there.

<!--CODE:03_unknown_is_not_invalid-->

```basic
' Recipe 3 — A blank field and a broken field are different answers.
'
' This is Axiom 7, and it is the recipe most likely to prevent a real defect.
'
' A field that is BLANK means the source said nothing: `unknown`. A field that
' is PRESENT and fails its own rule means the source said something that cannot
' be what it claims: `invalid`. Collapsing them loses the distinction the
' person reading the report needs — one is a gap to go and fill, the other is a
' defect to send back.
'
' The direction that actually hurts is reading a broken amount as ZERO. It
' understates the file, and an understated file looks exactly like a small one.
' So `value` is `unknown` in both cases — never 0, never "" — and the raw bytes
' survive either way so a human can see what was really there.
program main()
    load finio
    load finio_all

    reg = finio_all.registry()
    f {file}= "tests/finio/nacha/payroll.ach"
    payroll = read(f)

    doc = finio.read_text(reg, payroll, {})

    print "-- the file as it arrived"
    fld = doc.records[0].fields.reference_code
    print ("  reference_code   status " + fld.status
           + ", value is unknown: " + string(is_unknown(fld.value))
           + ", raw [" + fld.raw + "]")
    fld = doc.records[2].fields.individual_name
    print ("  individual_name  status " + fld.status
           + ", value [" + string(fld.value) + "]"
           + ", raw [" + fld.raw + "]")
    print ""

    ' Now damage one amount: an X where a digit belongs. This is what a bad
    ' export, a truncated transfer or a hand-edited file looks like.
    broken = replace(payroll, "0000125000EMP0001", "00001X5000EMP0001")
    bdoc = finio.read_text(reg, broken, {})

    print "-- the same file with one character changed"
    fld = bdoc.records[2].fields.amount
    print ("  amount           status " + fld.status)
    print ("  value is unknown: " + string(is_unknown(fld.value))
           + "   (and NOT zero: " + string(fld.value = 0) + ")")
    print ("  raw [" + fld.raw + "]  — the bytes are kept, so you can see what was there")
    print ("  why: " + string(fld.why))
    print ""

    ' The file still READ. Nothing raised. That is deliberate — see recipe 4.
    print ("records read: " + string(count(bdoc.records)))
    print ("issues found by validate: " + string(count(finio.validate(reg, bdoc))))
end program
```

<!--OUT:03_unknown_is_not_invalid-->

```
-- the file as it arrived
  reference_code   status unknown, value is unknown: true, raw [        ]
  individual_name  status ok, value [ALICE MERCER], raw [ALICE MERCER          ]

-- the same file with one character changed
  amount           status invalid
  value is unknown: true   (and NOT zero: false)
  raw [00001X5000]  — the bytes are kept, so you can see what was there
  why: an amount field must be digits

records read: 140
issues found by validate: 2
```

---

## 4. Reading and validating are different operations

§15: **invalid input is not necessarily unreadable input.**

A NACHA file states its own arithmetic — how many entries it holds, a hash of
the routing numbers it touched, its debit and credit totals — in control records
the producer computed. When those disagree with the entries, that is the single
most operationally important thing an ACH shop can be told. It is *not* a reason
to refuse to read the file. It is a reason to read it and report.

So `read_file` preserves and explains, `validate` judges, and the two are never
the same call. A library that raised on a control mismatch would hand you an
exception where you wanted a list of what is wrong.

The severity vocabulary is `error` / `warning` / `note`, and it grades
**conformance only** — how the source stands against the format. Whether a
mismatch is worth stopping a payment run for is your judgement, not the
library's, so there is no `critical` in it.

An adapter with no validator is **refused** rather than answered with an empty
list. An empty list means "this source conforms", the strongest claim available,
and returning it for an adapter that never looked would be indistinguishable
from a clean file.

<!--CODE:04_reading_is_not_validating-->

```basic
' Recipe 4 — Reading and validating are different operations.
'
' §15: invalid input is not necessarily unreadable input.
'
' A NACHA file states its own arithmetic — how many entries it holds, a hash of
' the routing numbers it touched, its debit and credit totals — in control
' records the producer computed. When those disagree with the entries, that is
' the single most operationally important thing an ACH shop can be told. It is
' NOT a reason to refuse to read the file. It is a reason to read it and
' report.
'
' So `read_file` preserves and explains; `validate` judges; and the two are
' never the same call. A library that raised on a control mismatch would hand
' you an exception where you wanted a list of what is wrong.
'
' The severity vocabulary is error / warning / note, and it grades CONFORMANCE
' only — how the source stands against the format. Whether a mismatch is worth
' stopping a payment run for is your judgement, not the library's, so there is
' no "critical" in it.
program main()
    load finio
    load finio_all

    reg = finio_all.registry()

    for each name in [ "payroll.ach", "bad_total.ach" ]
        doc = finio.read_file(reg, "tests/finio/nacha/" + name, {})
        issues = finio.validate(reg, doc)
        print (name)
        print ("  records read:  " + string(count(doc.records)))
        print ("  issues found:  " + string(count(issues)))
        for each i in issues
            where = ""
            if has(i, "record") then
                where = " (record " + string(i.record) + ")"
            end if
            print ("    " + i.severity + " " + i.code + where)
            print ("      " + i.message)
        end for
        print ""
    end for

    ' An adapter with no validator is REFUSED rather than answered with an
    ' empty list. An empty list means "this source conforms" — the strongest
    ' claim available — and returning it for an adapter that never looked would
    ' be indistinguishable from a clean file. Ask first if you need to know.
    a = finio.find_adapter(reg, "aba.nacha")
    print ("aba.nacha declares a validator: " + string(has(a, "validate")))
end program
```

<!--OUT:04_reading_is_not_validating-->

```
payroll.ach
  records read:  140
  issues found:  0

bad_total.ach
  records read:  140
  issues found:  1
    error batch_credit_total (record 6)
      the batch control's credit total is 4347.50; the entries give 4337.50

aba.nacha declares a validator: true
```

---

## 5. A fixed-width file need not have any newlines in it

A great many real fixed-width files arrive as one blocked run of records
straight off a mainframe, with no record separator at all. A reader that assumes
newlines reads such a file as **a single enormous record** — and then reports
one record, zero entries, and a clean bill of health.

So framing is part of recognition, not an assumption. The adapter looks at the
bytes and decides; `open_text` is told which. The terminator each record
actually carried is recorded too, so a writer can reproduce the file it read
rather than a normalised version of it.

The recipe asserts a **difference**: the same payments, framed three ways, must
give the same answer. Printing one of them alone would prove nothing.

**Measured, and better than that argument promises.** Perturbing the library so
the reader ignores what recognition told it does not produce a quiet misread —
it produces a refusal. Framing is decided once, in `recognise`, and both
recognition and reading go through that one `open_text` call, so a reader that
assumed newlines would fail to recognise the blocked file rather than reporting
one record and a clean bill of health. The silent outcome is what a naive
reader does; it is not a state this library can reach.

<!--CODE:05_framing_is_not_an_assumption-->

```basic
' Recipe 5 — A fixed-width file need not have any newlines in it.
'
' A great many real fixed-width files arrive as one blocked run of records
' straight off a mainframe, with no record separator at all. A reader that
' assumes newlines reads such a file as A SINGLE ENORMOUS RECORD — and then
' reports one record, zero entries and a clean bill of health.
'
' So framing is part of RECOGNITION, not an assumption. The adapter looks at
' the bytes, decides whether this file is line-framed or fixed-length blocked,
' and `open_text` is TOLD which. The same file is also recorded with the
' terminator each record actually carried, so a writer can reproduce the file
' it read rather than a normalised version of it.
'
' The assertion below is a DIFFERENCE: the same payments, framed three ways,
' must give the same answer. Printing one of them alone would prove nothing.
'
' MEASURED, and it is better than the argument above promises. Framing is
' decided ONCE and both recognition and reading go through that one decision,
' so a reader that ignored it would not misread the blocked file quietly -- it
' would fail to RECOGNISE it and refuse. The silent "one record, zero cents"
' outcome is what a naive reader does; it is not a state this library can
' reach.
program main()
    load finio
    load finio_all

    reg = finio_all.registry()

    for each name in [ "payroll.ach", "blocked.ach", "crlf.ach" ]
        f {file}= "tests/finio/nacha/" + name
        text = read(f)
        newlines = count(split(text, chr(10))) - 1

        doc = finio.read_file(reg, "tests/finio/nacha/" + name, {})
        credits = 0
        for each r in doc.records
            if r.kind = "entry_detail" and r.fields.amount.status = "ok" then
                if r.fields.transaction_code.direction = "credit" then
                    credits = credits + r.fields.amount.cents
                end if
            end if
        end for

        print (name)
        print ("  newlines in the file:  " + string(newlines))
        print ("  framing recognised:    " + doc.source.framing)
        print ("  records read:          " + string(count(doc.records)))
        print ("  credits, in cents:     " + string(credits))
        print ""
    end for
    print ""
    print "Same file, three framings, one answer."
    print "A reader that assumed newlines would read the blocked file as one"
    print "94*140-byte record. Here it would not get that far: framing is"
    print "decided once, and recognition goes through the same decision, so"
    print "such a reader refuses the file rather than misreading it."
end program
```

<!--OUT:05_framing_is_not_an_assumption-->

```
payroll.ach
  newlines in the file:  140
  framing recognised:    lines
  records read:          140
  credits, in cents:     452890

blocked.ach
  newlines in the file:  0
  framing recognised:    fixed
  records read:          140
  credits, in cents:     452890

crlf.ach
  newlines in the file:  140
  framing recognised:    lines
  records read:          140
  credits, in cents:     452890


Same file, three framings, one answer.
A reader that assumed newlines would read the blocked file as one
94*140-byte record. Here it would not get that far: framing is
decided once, and recognition goes through the same decision, so
such a reader refuses the file rather than misreading it.
```

---

## 6. One call, five formats, four structural shapes

This is the recipe the framework exists for. A fixed-width ACH file, a delimited
BAI2 statement, two XML messages and a tagged OFX download have nothing in
common at the byte level. Above `finio` they are the same shape.

Nothing in the recipe names a format.

The one place the difference survives, deliberately, is `byte_fidelity`. NACHA
and BAI2 can be reproduced byte for byte from what was read; camt.053 cannot,
because an XML document carries whitespace and attribute order that a reader
does not model. Saying so beats claiming a guarantee that is false — and the
answer travels **with** the document rather than being looked up somewhere else
by a caller who may not think to.

<!--CODE:06_one_call_five_formats-->

```basic
' Recipe 6 — One call, five formats, four structural shapes.
'
' This is the recipe the framework exists for. A fixed-width ACH file, a
' delimited BAI2 statement, two XML messages and a tagged OFX download have
' nothing in common at the byte level: different framings, different record
' models, different ways of saying an amount. Above `finio` they are the same
' shape — a document with records, entities and a loss report.
'
' Nothing below names a format. The registry recognises each file from its own
' evidence.
'
' The one place the difference survives, deliberately, is `byte_fidelity`.
' NACHA and BAI2 can be reproduced byte for byte from what was read; camt.053
' cannot, because an XML document carries whitespace and attribute order that a
' reader does not model. Saying so beats claiming a guarantee that is false.
program main()
    load finio
    load finio_all

    reg = finio_all.registry()

    files = [ "tests/finio/nacha/payroll.ach",
              "tests/finio/bai2/statement.bai",
              "tests/finio/camt/statement.xml",
              "tests/finio/ofx/statement_v1.ofx",
              "tests/finio/pain/payments.xml" ]

    for each path in files
        doc = finio.read_file(reg, path, {})
        kinds = []
        for each r in doc.records
            if not contains(kinds, r.kind) then
                append(kinds, r.kind)
            end if
        end for
        print (file_name(path))
        print ("  adapter        " + doc.adapter + " (" + doc.classification + ")")
        print ("  revision       " + string(doc.revision))
        print ("  records        " + string(count(doc.records)))
        print ("  record kinds   " + join(kinds, ", "))
        print ("  entities       " + join(keys(doc.entities), ", "))
        print ("  byte fidelity  " + string(doc.byte_fidelity))
        print ""
    end for
end program
```

<!--OUT:06_one_call_five_formats-->

```
payroll.ach
  adapter        aba.nacha (exact)
  revision       unresolved
  records        140
  record kinds   file_header, batch_header, entry_detail, addenda, batch_control, file_control, padding
  entities       header, batches, control, padding_records
  byte fidelity  true

statement.bai
  adapter        bai2 (exact)
  revision       2
  records        12
  record kinds   file_header, group_header, account_identifier, transaction_detail, account_trailer, group_trailer, file_trailer
  entities       header, groups, trailer
  byte fidelity  true

statement.xml
  adapter        iso20022.camt053 (exact)
  revision       camt.053.001.08
  records        16
  record kinds   group_header, statement, balance, summary, entry
  entities       header, statements
  byte fidelity  false

statement_v1.ofx
  adapter        ofx (exact)
  revision       102
  records        10
  record kinds   status, signon, account, transaction, transaction_list, balance, statement
  entities       signon, statements
  byte fidelity  true

payments.xml
  adapter        iso20022.pain001 (exact)
  revision       pain.001.001.03
  records        7
  record kinds   group_header, payment_info, transaction
  entities       header, blocks
  byte fidelity  true

```

---

## 7. What the reader did *not* claim

Axiom 8: loss is explicit. §18: what the layout did not claim is preserved as
unknown rather than dropped. Local interpretation usually lives exactly there —
the field your bank uses for a cost centre, the record type a vendor added, the
trailing bytes nobody documented.

Two mechanisms, catching different things.

`coverage` is arithmetic over a fixed-width layout: the fields account for some
of the record and the rest is a **gap**. An adapter that quietly forgot a field
shows up here as a gap rather than as nothing at all. An **overlap** is the
other half, and it is what transcribing a published guide produces — those
number positions from 1, so a layout copied straight out is off by one
everywhere.

`doc.loss` is the reader's own report of bytes it did not interpret. The bytes
are kept either way: a loss note says what was not claimed, it does not throw
anything away.

<!--CODE:07_what_was_not_claimed-->

```basic
' Recipe 7 — What the reader did NOT claim.
'
' Axiom 8: loss is explicit. §18: what the layout did not claim is preserved as
' unknown rather than dropped. Local interpretation usually lives exactly
' there — the field your bank uses for a cost centre, the record type a vendor
' added, the trailing bytes nobody documented.
'
' Two mechanisms, and they catch different things.
'
' `coverage` is arithmetic over a fixed-width layout: the fields account for
' some of the record and the rest is a GAP. An adapter that quietly forgot a
' field shows up here — as a gap, rather than as nothing at all. An OVERLAP is
' the other half, and it is what transcribing a published guide produces, since
' those number positions from 1 and a layout copied straight out is off by one
' everywhere.
'
' `doc.loss` is the reader's own report of bytes it did not interpret. The
' bytes are KEPT either way — a loss note says what was not claimed, it does
' not throw anything away.
program main()
    load finio
    load finio_all
    load finio_nacha

    reg = finio_all.registry()

    print "-- coverage: does each layout account for all 94 bytes?"
    for each kind in [ "file_header", "entry_detail", "batch_control" ]
        cov = finio.coverage(finio_nacha.layout_for(kind), 94)
        print ("  " + kind + ": complete " + string(cov.complete)
               + ", gaps " + string(count(cov.gaps))
               + ", overlaps " + string(count(cov.overlaps)))
    end for
    print ""

    print "-- and a layout with a field removed reports the hole it left"
    full = finio_nacha.layout_for("entry_detail")
    short = []
    for each spec in full
        if spec.concept != "individual_name" then
            append(short, spec)
        end if
    end for
    cov = finio.coverage(finio.layout(short), 94)
    print ("  complete " + string(cov.complete) + ", gaps " + string(count(cov.gaps)))
    for each g in cov.gaps
        print ("  gap at byte " + string(g.offset) + " for " + string(g.length) + " bytes")
    end for
    print ""

    ' A record type this adapter does not interpret. Turn the addenda record
    ' (type 7) into a type 4, which NACHA does not define and finio_nacha
    ' therefore does not claim.
    f {file}= "tests/finio/nacha/payroll.ach"
    text = read(f)
    odd = replace(text, "705PAYROLL", "405PAYROLL")
    doc = finio.read_text(reg, odd, {})

    print "-- a record type the adapter does not interpret"
    print ("  records read: " + string(count(doc.records)) + "   (nothing was dropped)")
    print ("  loss notes:   " + string(count(doc.loss)))
    for each l in doc.loss
        print ("    " + l.kind + ", record " + string(l.record)
               + ", bytes " + string(l.byte_offset) + " + " + string(l.byte_length))
        print ("    " + l.why)
    end for
end program
```

<!--OUT:07_what_was_not_claimed-->

```
-- coverage: does each layout account for all 94 bytes?
  file_header: complete true, gaps 0, overlaps 0
  entry_detail: complete true, gaps 0, overlaps 0
  batch_control: complete true, gaps 0, overlaps 0

-- and a layout with a field removed reports the hole it left
  complete false, gaps 1
  gap at byte 54 for 22 bytes

-- a record type the adapter does not interpret
  records read: 140   (nothing was dropped)
  loss notes:   1
    uninterpreted, record 4, bytes 380 + 94
    record type '4' is not one this adapter interprets; the bytes are kept
```

---

## 8. Writing, and the two ways it can go wrong

Reading a file you were sent is forgiving: whatever arrived, arrived. Writing
one you will **send** is not. A pain.001 credit transfer goes to a bank, and a
creditor name three characters too long does not come back as an error — it
comes back as a payment to a truncated name, or silently not at all.

So §16 asks **before** serialising anything, and there are three answers:

| classification | meaning | override |
|---|---|---|
| `representable` | every value fits what the target carries | — |
| `lossy` | it would fit if something were shortened | `allow_lossy` |
| `impossible` | the target format does not carry this at all | **none** |

The default favours refusal. A lossy write is refused unless you say otherwise,
and the loss is reported either way — so it can be *permitted* but not *silent*.
An impossible write has no override, because that is a fact about the target
format and not a cost you can choose to accept.

Note what is being checked: the **scheme's** limits, not the schema's. The XSD
would accept a 140-character creditor name. SEPA carries 70. A document that
validates against the schema and gets rejected by the bank is the ordinary case,
not the exotic one.

<!--CODE:08_writing_and_refusing-->

```basic
' Recipe 8 — Writing, and the two ways it can go wrong.
'
' Reading a file you were sent is forgiving: whatever arrived, arrived. Writing
' one you will SEND is not. A pain.001 credit transfer goes to a bank, and a
' creditor name three characters too long does not come back as an error — it
' comes back as a payment to a truncated name, or silently not at all.
'
' So §16 asks BEFORE serialising anything, and there are three answers:
'
'   representable  every value fits what the target carries
'   lossy          it would fit if something were shortened
'   impossible     the target format does not carry this at all
'
' The default favours refusal. A LOSSY write is refused unless you say
' otherwise — the loss is reported either way, so it can be permitted but not
' silent. An IMPOSSIBLE write has NO override, because that is a fact about the
' target format and not a cost you can choose to accept.
'
' Note what is being checked: the SCHEME's limits, not the schema's. The XSD
' would accept a 140-character creditor name. SEPA carries 70. A document that
' validates against the schema and gets rejected by the bank is the ordinary
' case, not the exotic one.
program main()
    load finio
    load finio_all
    load finio_pain001

    reg = finio_all.registry()

    print ("the scheme's limits: " + string(finio_pain001.scheme_limits()))
    print ""

    for each name in [ "payments.xml", "long_name.xml", "mixed_currency.xml" ]
        doc = finio.read_file(reg, "tests/finio/pain/" + name, {})
        c = finio.classify_write(reg, doc)
        print (name + "  ->  " + c.classification)
        print ("  " + string(c.why))

        ' Try to write it the ordinary way and see what happens.
        on error goto next
        out = finio.write_text(reg, doc)
        if error then
            print ("  refused: " + error.message)
            error.clear()
        else
            print ("  written: " + string(byte_count(out.text)) + " bytes, "
                   + "byte fidelity " + string(out.byte_fidelity))
        end if
        on error stop
        print ""
    end for

    ' The lossy one, permitted. The loss is still reported.
    doc = finio.read_file(reg, "tests/finio/pain/long_name.xml", {})
    out = finio.write_text(reg, doc, true)
    print ("long_name.xml written with allow_lossy: "
           + string(byte_count(out.text)) + " bytes")
    print ("  classification stays " + out.classification
           + ", loss notes " + string(count(out.loss)))

    ' And the impossible one stays impossible, however hard you ask.
    doc = finio.read_file(reg, "tests/finio/pain/mixed_currency.xml", {})
    on error goto next
    out = finio.write_text(reg, doc, true)
    if error then
        print "mixed_currency.xml with allow_lossy: still refused"
        error.clear()
    else
        print "mixed_currency.xml with allow_lossy: WROTE IT (this would be a bug)"
    end if
    on error stop
end program
```

<!--OUT:08_writing_and_refusing-->

```
the scheme's limits: {"creditor_name":70,"debtor_name":70,"remittance":140,"end_to_end_id":35,"instruction_id":35,"payment_info_id":35,"message_id":35}

payments.xml  ->  representable
  every value fits what the scheme carries
  written: 3155 bytes, byte fidelity true

long_name.xml  ->  lossy
  creditor is 73 characters and the scheme carries 70; writing it would truncate 3
  refused: finio.write_text: writing this document as iso20022.pain001 would LOSE information -- creditor is 73 characters and the scheme carries 70; writing it would truncate 3. Pass allow_lossy to write it anyway; the loss is reported either way.

mixed_currency.xml  ->  impossible
  the SEPA credit transfer scheme carries EUR and this amount is in USD; no truncation makes it representable
  refused: finio.write_text: this document CANNOT be written as iso20022.pain001 -- the SEPA credit transfer scheme carries EUR and this amount is in USD; no truncation makes it representable. There is no override: the target format does not carry it.

long_name.xml written with allow_lossy: 1833 bytes
  classification stays lossy, loss notes 1
mixed_currency.xml with allow_lossy: still refused
```

---

## 9. The registry: what exists, what may be built, and what may not

§9 and §10. The point is one line of the design: **finding that a format exists
is not the same as possessing enough legitimate information to implement it.**

So a registry entry records where the specification came from, when it was
retrieved, and whether the rights permit implementing it. An entry claiming
`implemented` with no specification source is refused — that is exactly the
claim a registry exists to keep honest.

The acquisition classes say why a format is or is not reachable:

| class | meaning |
|---|---|
| `OPEN` | published, free, implementable now |
| `PUBLIC_VENDOR` | a vendor publishes it openly |
| `DE_FACTO` | the authority does not publish, but consistent public implementation documentation exists |
| `CONTROLLED` | the specification is licensed; a person must buy it |
| `HUMAN_REQUIRED` | reachable, but not by an automated fetch |
| `INSUFFICIENT` | not enough public information to build from |

`CONTROLLED` is not a to-do item. It is a queue entry that needs a purchase
order, and saying so is more useful than an adapter guessed at from
reverse-engineering.

The last block is the honest part. `aba.nacha` has been run against ten ACH
files this project did not write, so its recognition and reading are `verified`
— but no specification was ever obtained, the Nacha Operating Rules being a paid
publication, so the entry's own state stays `researched`. Those are different
axes, and collapsing them would have to lie in one direction or the other.

<!--CODE:09_the_acquisition_queue-->

```basic
' Recipe 9 — The registry: what exists, what may be built, and what may not.
'
' §9 and §10. The point is stated in one line of the design: finding that a
' format exists is not the same as possessing enough legitimate information to
' implement it.
'
' So a registry entry records where the specification came from, when it was
' retrieved, and whether the rights permit implementing it. An entry claiming
' `implemented` with no specification source is REFUSED — that is exactly the
' claim a registry exists to keep honest.
'
' The acquisition classes say why a format is or is not reachable:
'
'   OPEN            published, free, implementable now
'   PUBLIC_VENDOR   a vendor publishes it openly
'   DE_FACTO        the authority does not publish, but consistent public
'                   implementation documentation exists
'   CONTROLLED      the specification is licensed; a person must buy it
'   HUMAN_REQUIRED  reachable, but not by an automated fetch
'   INSUFFICIENT    not enough public information to build from
'
' CONTROLLED is not a to-do. It is a queue item that needs a purchase order,
' and saying so is more useful than a guess built from reverse-engineering.
program main()
    load finio
    load finio_all
    load finio_registry

    entries = finio_registry.all(finio_all.adapters())

    print "-- what the queue holds"
    cov = finio_registry.coverage(entries)
    print ("  formats:   " + string(cov.entries))
    print ("  families:  " + join(cov.families_present, ", "))
    print ("  §14 names " + string(cov.design_domains) + " candidate domains")
    print ""

    print "-- by acquisition class"
    byclass = finio_registry.by_acquisition_class(entries)
    for each c in finio.acquisition_classes()
        ids = byclass[c]
        if count(ids) > 0 then
            print ("  " + c + ": " + join(ids, ", "))
        end if
    end for
    print ""

    print "-- what can be built from what we legitimately hold"
    print ("  implementable: " + string(count(finio_registry.implementable(entries))))
    print ("  blocked:       " + string(count(finio_registry.blocked(entries))))
    for each b in finio_registry.blocked(entries)
        print ("    " + b.id + " (" + b.acquisition_class + ")")
        print ("      " + b.blocked_by)
    end for
    print ""

    ' The evidence a shipped adapter carries. `finio.check_registry_entry`
    ' refuses an entry that claims more than it can show.
    print "-- the evidence behind one shipped adapter"
    for each e in entries
        if e.id = "bai2" then
            for each s in e.specification_sources
                print ("  " + s.source_type + ", retrieved " + s.date_retrieved)
            end for
            print ("  implementation allowed:       " + string(e.implementation_allowed))
            print ("  spec may be redistributed:    " + string(e.spec_redistribution_allowed))
            print ("  samples may be redistributed: " + string(e.sample_redistribution_allowed))
        end if
    end for
    print ""

    ' THE STATE AND THE PER-CAPABILITY STATUSES ARE DIFFERENT AXES, and keeping
    ' them apart is the whole reason for having both. `aba.nacha` has been run
    ' against ten ACH files this project did not write, so its recognition and
    ' reading are `verified` — but no specification was ever obtained (the
    ' Nacha Operating Rules are a paid publication), so the entry's own state
    ' stays `researched`. Collapsing the two would have to lie in one direction
    ' or the other.
    print "-- one entry, two axes"
    for each e in entries
        if e.id = "aba.nacha" then
            print ("  state              " + e.state)
            print ("  acquisition class  " + e.acquisition_class)
            print ("  recognition        " + e.recognition_status)
            print ("  read               " + e.read_status)
            print ("  validation         " + e.validation_status)
            print ("  write              " + e.write_status)
        end if
    end for
end program
```

<!--OUT:09_the_acquisition_queue-->

```
-- what the queue holds
  formats:   10
  families:  cash management, payments, cards, securities, healthcare remittance, account aggregation
  §14 names 22 candidate domains

-- by acquisition class
  OPEN: fix, iso20022.camt053, bai2, ofx, iso20022.pain001
  CONTROLLED: x12.820, iso8583, x12.835
  DE_FACTO: swift.mt940, aba.nacha

-- what can be built from what we legitimately hold
  implementable: 7
  blocked:       3
    x12.820 (CONTROLLED)
      the transaction-set specification is licensed rather than published; a licence must be acquired by a person before an adapter can be written from the specification rather than from guesswork
    iso8583 (CONTROLLED)
      the standard must be purchased, and beyond it each card scheme's own element definitions are issued under agreement to participants -- so even a purchased copy does not make a usable adapter
    x12.835 (CONTROLLED)
      the implementation guide is licensed; free examples permit recognition but not interpretation

-- the evidence behind one shipped adapter
  standards_body, retrieved 2026-09-15
  implementation_guide, retrieved 2026-09-15
  public_sample, retrieved 2026-09-15
  implementation allowed:       true
  spec may be redistributed:    false
  samples may be redistributed: false

-- one entry, two axes
  state              researched
  acquisition class  DE_FACTO
  recognition        verified
  read               verified
  validation         implemented
  write              re-emission only; this adapter does not originate a file
```

---

## 10. Noticing that a format moved

An adapter is not finished when it passes its tests. Formats move: ISO 20022
publishes a new message version every year, a card scheme adds a field, a bank
changes what it puts in the narrative. §13: **staleness should be derivable
rather than remembered.**

Two signals, and §13's argument is that neither is much use alone.

A **watch** looks at a published source and reports one of four findings. The
fourth is the one that matters: `unreachable` is not a quiet `unchanged`. A
source that has been 403-ing for six months is not a stable format, it is a
watch that stopped working — and reporting it as "no change" is how a monitoring
process comes to assert the world is still by observing nothing. This is not
hypothetical: Nacha's own developer guide returned 403 to an automated fetch
during the first survey, and an `unchanged` there would have been a lie about
ACH.

An **observation** is what production noticed: a code the adapter could not
explain, a field it did not expect. One is a curiosity; three hundred across
last quarter is a stronger signal that a revision happened than any watch list,
and it names the field to go and read about.

Observations record **a token and a location, never content**, and it is
enforced rather than asked for: a `detail` longer than 64 bytes is refused,
because a whole record passed as a "token" is a customer record in a log.

What is retained between checks is a **fingerprint**, not the page. Keeping the
page would make the registry a cache of other people's documents, which is a
redistribution question as well as a storage one — every entry here says
`spec_redistribution_allowed: false`.

And a **dormant** format is not overdue. ISO 8583's last revision is from 2003
and nobody expects it to move, so it is declared dormant and never appears in
the queue. Without that, the queue fills with formats nobody expects to move and
the ones that do are buried — which is how a maintenance list stops being read.

<!--CODE:10_keeping_them_current-->

```basic
' Recipe 10 — Noticing that a format moved.
'
' An adapter is not finished when it passes its tests. Formats move: ISO 20022
' publishes a new message version every year, a card scheme adds a field, a
' bank changes what it puts in the narrative. §13 says staleness should be
' DERIVABLE rather than remembered.
'
' Two signals, and §13's argument is that neither is much use alone.
'
' A WATCH looks at a published source and reports one of four findings. The
' fourth is the one that matters: `unreachable` is NOT a quiet `unchanged`. A
' source that has been 403-ing for six months is not a stable format, it is a
' watch that stopped working — and this is not hypothetical, since Nacha's own
' developer guide returned 403 to an automated fetch during the first survey.
'
' An OBSERVATION is what production noticed: a code the adapter could not
' explain, a field it did not expect. One is a curiosity; three hundred across
' last quarter is a stronger signal that a revision happened than any watch
' list, and it names the field to go and read about.
'
' Observations record A TOKEN AND A LOCATION, never content, and that is
' ENFORCED — a `detail` longer than 64 bytes is refused, because a whole record
' passed as a "token" is a customer record in a log.
program main()
    load finio
    load finio_all
    load finio_registry
    load finio_watch

    today = "2026-09-16"

    ' --- a watch, checked four ways -----------------------------------------
    print "-- the four findings"
    baseline = finio_watch.check(
        finio_watch.source("document",
            { reference: "https://example.invalid/bai2-guide.pdf",
              watching: "a change to the record-code table" }),
        { ok: true, content: "record codes 01 02 03 16 49 88 98 99" }, today)
    print ("  " + baseline.finding + ": " + baseline.means)

    seen = finio_watch.source("document",
        { reference: "https://example.invalid/bai2-guide.pdf",
          watching: "a change to the record-code table",
          last_seen: baseline.fingerprint })

    same = finio_watch.check(seen, { ok: true, content: "record codes 01 02 03 16 49 88 98 99" }, today)
    print ("  " + same.finding + " (nothing to report)")

    moved = finio_watch.check(seen, { ok: true, content: "record codes 01 02 03 16 49 88 90 98 99" }, today)
    print ("  " + moved.finding + ": " + moved.means)

    gone = finio_watch.check(seen, { ok: false, why: "HTTP 403" }, today)
    print ("  " + gone.finding + " (" + gone.why + "): " + gone.means)
    print ""

    ' --- what production saw ------------------------------------------------
    log = {}
    for each n in [ 1, 2, 3, 4 ]
        log = finio_watch.observe(log,
            { adapter: "aba.nacha", revision: "unresolved",
              kind: "unknown_code", detail: "service class 280",
              where: "batch " + string(n), seen: today })
    end for
    log = finio_watch.observe(log,
        { adapter: "bai2", revision: "2", kind: "unknown_code",
          detail: "type code 921", where: "row 14", seen: today })

    print "-- what production could not explain"
    for each o in finio_watch.observations_for(log, "aba.nacha")
        print ("  " + o.adapter + ": " + o.kind + " " + o.detail
               + ", seen " + string(o.occurrences) + " times")
        print ("    examples: " + join(o.where, ", "))
    end for
    print ""

    ' A detail that is not a token is refused, because it would be content.
    on error goto next
    log = finio_watch.observe(log,
        { adapter: "bai2", revision: "2", kind: "unknown_field",
          detail: "16,409,10000,,,ACME WIDGETS INC PAYROLL 021000021 ACCT 12345678901234/",
          seen: today })
    if error then
        print "-- a whole record offered as a token"
        print ("  refused: " + error.message)
        error.clear()
    end if
    on error stop
    print ""

    ' --- the two together ---------------------------------------------------
    entries = finio_registry.all(finio_all.adapters())
    print "-- the review queue, a year from now"
    queue = finio_watch.review_queue(entries, log, "2027-10-01")
    for each q in queue
        print ("  " + q.id + "  (rank " + string(q.rank) + ", " + q.priority + ")")
        print ("    " + q.why)
    end for
    print ""

    ' A DORMANT FORMAT IS NOT OVERDUE. ISO 8583's last revision is from 2003 and
    ' nobody expects it to move, so it is declared `dormant` and never appears
    ' here. Without that, the queue fills with formats nobody expects to move
    ' and the ones that do are buried — which is how a maintenance list stops
    ' being read.
    listed = []
    for each q in queue
        append(listed, q.id)
    end for
    print ("  formats in the registry: " + string(count(entries)))
    print ("  formats in the queue:    " + string(count(queue)))
    for each e in entries
        if not contains(listed, e.id) then
            print ("  absent: " + e.id + " (" + e.maintenance_priority + ")")
        end if
    end for
end program
```

<!--OUT:10_keeping_them_current-->

```
-- the four findings
  first_sight: nothing was known about this source before; this is the baseline
  unchanged (nothing to report)
  changed: a change to the record-code table
  unreachable (HTTP 403): this watch is not working; it says nothing about whether https://example.invalid/bai2-guide.pdf has moved

-- what production could not explain
  aba.nacha: unknown_code service class 280, seen 4 times
    examples: batch 1, batch 2, batch 3, batch 4

-- a whole record offered as a token
  refused: finio_watch.observation: the detail is 70 bytes and an observation records a TOKEN, not content (limit 64). §9: observations record tokens and locations, never the content of a customer record.

-- the review queue, a year from now
  aba.nacha  (rank 3, active)
    review is due, and 1 kind(s) of thing this adapter could not explain have been seen 4 time(s)
  bai2  (rank 3, periodic)
    review is due, and 1 kind(s) of thing this adapter could not explain have been seen 1 time(s)
  swift.mt940  (rank 1, periodic)
    review is due
  x12.820  (rank 1, periodic)
    review is due
  fix  (rank 1, periodic)
    review is due
  x12.835  (rank 1, periodic)
    review is due
  iso20022.camt053  (rank 1, active)
    review is due
  ofx  (rank 1, periodic)
    review is due
  iso20022.pain001  (rank 1, active)
    review is due

  formats in the registry: 10
  formats in the queue:    9
  absent: iso8583 (dormant)
```

---

## What this page does not show

Stated plainly, because a cookbook that only shows what works teaches the wrong
confidence.

- **`finio_camt` has never met a bank's statement.** Every camt.053 file it has
  been run against was written here or generated from the schema. The publicly
  available real samples returned HTTP 403 to an automated fetch and need a
  person to retrieve them. Its registry entry says `discovered`, not
  `researched`, for that reason.
- **The survey has no denominator.** Ten formats across six families, against
  the design's own list of twenty-two candidate domains. Formats named in §14
  and absent from the registry are absent because nobody has looked, not because
  they were ruled out.
- **Three formats are blocked on a purchase**, not on work: X12 820, X12 835 and
  ISO 8583. Recipe 9 names them and why.
- **Two location kinds have no callers yet** — `spreadsheet` and `json`. They
  are in the model because §4 named them; nothing has needed them.
