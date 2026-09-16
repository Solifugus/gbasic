# Financial file formats: a tutorial

One continuous problem, worked end to end. A directory of files has arrived
from a counterparty overnight; by the end of this page you will have found out
what they are, read them, established which ones hold together, discovered the
ones the specification does not permit and real producers send anyway, counted
what the reader could not explain, and sent one back with a field corrected.

**This page cannot lie.** Every code block below is a real file under
`examples/finio_tutorial/`, and every output block is that file's actual stdout,
compared byte for byte by `tests/run_finio_tutorial.sh`.

Task-oriented recipes instead: [finio_cookbook.md](finio_cookbook.md). Design
and rationale: [financial_adapters_design.md](financial_adapters_design.md).
Per-function reference: [reference.md](reference.md#finio).

Run any step yourself:

```
GBASIC_PATH=stdlib ./gbasic examples/finio_tutorial/01_what_arrived.bas
```

---

## The files

Every step on this page reads the same ten files: real ACH files from the
[moov-io/ach](https://github.com/moov-io/ach) project, Apache-2.0, redistributed
under `tests/finio/foreign/` with their licence and a provenance manifest.

**That they were written by somebody else is the only reason this page is worth
anything.** A generator we wrote shares our misunderstandings, so a fixture
written here can only ever confirm them. These ten files found three defects no
fixture in this project could — and steps 2 and 3 are the shape of those
findings, not a demonstration arranged to succeed.

---

## Step 1 — What is in this directory?

Some of what lands in a drop directory is a payment file. Some is a licence, a
manifest, a README the transfer swept along. Before you can do anything you have
to know which is which, and the filenames will not tell you — the extension is
whatever the producer felt like.

`finio.scan` walks a directory, asks each file what it is, and tallies.

It never raises on one bad file. A file whose bytes cannot be read is its **own
outcome**, reported separately from one that read fine and matched nothing —
because an operator deciding what to do about eleven unknown files needs to know
which of them nobody could even open, and letting one bad file end a scan over
an archive is not an answer at all.

<!--CODE:01_what_arrived-->

```basic
' Step 1 — What is in this directory?
'
' A night's files land in a drop directory. Some are payment files. Some are
' a licence, a manifest, a README the transfer swept along. Before you can do
' anything you have to know which is which, and the filenames will not tell you
' — the extension is whatever the producer felt like.
'
' `finio.scan` walks a directory, asks each file what it is, and tallies. It
' never raises on one bad file: a file whose bytes cannot be read is its own
' outcome, because an operator deciding what to do about eleven unknown files
' needs to know which of them nobody could even open.
'
' The files here are real ACH files from the moov-io/ach project — written by
' somebody else, which is the only reason this step is worth anything. A
' generator we wrote shares our misunderstandings.
program main()
    load finio
    load finio_all

    reg = finio_all.registry()
    found = finio.scan(reg, "tests/finio/foreign/")

    print ("files examined: " + string(found.examined))
    print ""

    print "recognised:"
    for each k in keys(found.counts)
        print ("  " + k + "   " + string(found.counts[k]) + " files")
    end for
    print ""

    print "not recognised as anything in the registry:"
    for each p in found.unknown
        print ("  " + file_name(p))
    end for
    print ""

    print ("ambiguous (two adapters both claim it): " + string(count(found.ambiguous)))
    print ("unreadable:                             " + string(count(found.unreadable)))
    print ""
    print "The two unknowns are a licence and a provenance manifest. That is the"
    print "right answer: they are not payment files, and a scan that had found a"
    print "format for them would be telling you something false."
end program
```

<!--OUT:01_what_arrived-->

```
files examined: 12

recognised:
  aba.nacha   10 files

not recognised as anything in the registry:
  LICENSE-moov-io-ach
  PROVENANCE.txt

ambiguous (two adapters both claim it): 0
unreadable:                             0

The two unknowns are a licence and a provenance manifest. That is the
right answer: they are not payment files, and a scan that had found a
format for them would be telling you something false.
```

---

## Step 2 — Which ones can you trust?

Every one of the ten **reads**. That is §15 working: reading and validating are
different operations, so a file whose control totals disagree with its entries
still comes back as a document you can look at.

Now ask the second question. A NACHA file states its own arithmetic — entry
counts, a hash of the routing numbers it touched, debit and credit totals — in
control records its producer computed. `validate` holds the file to numbers it
supplied itself, which is the strongest kind of check available: the oracle came
with the file.

**Two of the ten are completely clean.** That is the ordinary result, not a
disappointing one. Real files carry returns, reversals, corrections and batch
types a general reader does not implement, and each of those shows up here as
something to look at rather than as a silent wrong number.

The second half of the step groups issues by code, because that is a different
question. A code appearing once is a fact about that file. A code repeated
across files is a fact about the corpus — and `record_length`, six of twenty,
is the one worth following. Step 3 follows it.

<!--CODE:02_which_ones_can_you_trust-->

```basic
' Step 2 — Read them all, and find out which ones hold together.
'
' Every one of these ten files READS. That is §15 working: reading and
' validating are different operations, so a file whose control totals disagree
' with its entries still comes back as a document you can look at.
'
' Now ask the second question. A NACHA file states its own arithmetic — entry
' counts, a hash of the routing numbers it touched, debit and credit totals —
' in control records its producer computed. `validate` holds the file to
' numbers it supplied itself.
'
' Two of the ten are completely clean. That is not a disappointing result, it
' is the ordinary one: real files carry returns, reversals, corrections and
' batch types a general reader does not implement, and each of those shows up
' here as something to look at rather than as a silent wrong number.
program main()
    load finio
    load finio_all

    reg = finio_all.registry()
    found = finio.scan(reg, "tests/finio/foreign/")

    clean = 0
    total_issues = 0
    for each path in found.files
        if contains(found.unknown, path) then
            continue
        end if
        doc = finio.read_file(reg, path, {})
        issues = finio.validate(reg, doc)
        total_issues = total_issues + count(issues)
        mark = "  "
        if count(issues) = 0 then
            clean = clean + 1
            mark = "ok"
        end if
        print (mark + "  " + file_name(path)
               + "   " + string(count(doc.records)) + " records"
               + ",  issues " + string(count(issues)))
    end for
    print ""
    print (string(clean) + " of 10 validate completely clean; "
           + string(total_issues) + " issues in all")
    print ""

    ' What kind of thing is wrong? Group by code rather than reading ten
    ' reports: a code repeated across files is a property of the CORPUS, and a
    ' code appearing once is a property of that file.
    by_code = {}
    for each path in found.files
        if contains(found.unknown, path) then
            continue
        end if
        doc = finio.read_file(reg, path, {})
        for each i in finio.validate(reg, doc)
            if has(by_code, i.code) then
                by_code[i.code] = by_code[i.code] + 1
            else
                by_code[i.code] = 1
            end if
        end for
    end for

    print "what kind of thing is wrong:"
    for each c in keys(by_code)
        print ("  " + c + "   " + string(by_code[c]))
    end for
end program
```

<!--OUT:02_which_ones_can_you_trust-->

```
    20110729A-invalid.ach   293 records,  issues 6
    20110805A.ach   93 records,  issues 2
    20180713-IAT.ach   30 records,  issues 1
    FISERV-ZEROFILE.ach   10 records,  issues 2
ok  NACHA_SAMPLE_TEL_REVERSAL.ach   10 records,  issues 0
    adv.ach   10 records,  issues 5
    cor-example.ach   10 records,  issues 1
    extended-ascii.ach   20 records,  issues 1
ok  gl-debit.ach   10 records,  issues 0
    ppd-debit.ach   10 records,  issues 2

2 of 10 validate completely clean; 20 issues in all

what kind of thing is wrong:
  record_length   6
  blocking   2
  header_declaration   3
  file_batch_count   2
  batch_credit_total   1
  unsupported_batch_kind   1
  unknown_transaction_code   3
  file_credit_total   1
  non_ascii   1
```

---

## Step 3 — The files the specification does not permit

NACHA says every record is 94 bytes. Four of these ten have records that are
not — 93, 75, 69, 55, and one of 95.

**They are not corrupt.** Their producer stripped trailing blanks, so a file
header arrives at 75 bytes and a file control at exactly 55. Nothing is missing
except the blank part.

A reader with a strict 94-byte rule refuses the file outright. That is the wrong
answer: the file is perfectly usable and the fields that are there are correct.
It is also what this adapter did until these ten files were run through it.

So what is the right answer? **Axiom 7 settles it**, and it settles it in a way
worth stating precisely, because there are two cases and they differ:

- A field **past the end** of a short record is one the source said nothing
  about. `unknown` — not blank, not zero. Padding the record to 94 would turn
  "absent" into "blank", and those are the two things Axiom 7 exists to keep
  apart.
- A field only **partly present** is `invalid`, never a shorter value. Reading
  the first six digits of a truncated ten-digit amount gives a perfectly
  ordinary number a hundred times too small, with nothing raised anywhere.

One file carries non-ASCII: 94 codepoints in 95 bytes. The specification does
not permit that either, and it is reported and read — and it is why every offset
in this library is a **byte** offset rather than a codepoint one. One UTF-8
É inside a 22-byte name field shifts every later field one place left, and a
15-digit trace number comes back as an ordinary-looking 13-digit one.

<!--CODE:03_files_the_spec_does_not_permit-->

```basic
' Step 3 — The files the specification does not permit, which real producers
' send anyway.
'
' `record_length` was the biggest bucket in step 2, and it is the most
' instructive. NACHA says every record is 94 bytes. Four of these ten files
' have records that are not.
'
' They are not corrupt. Their producer stripped TRAILING BLANKS, so a file
' header arrives at 75 bytes and a file control at exactly 55 — nothing is
' missing except the blank part. A reader with a strict 94-byte rule refuses
' the file outright, which is the wrong answer: the file is perfectly usable
' and the fields that are there are correct.
'
' What is the RIGHT answer? Axiom 7 settles it. A field past the end of a short
' record is one the source said nothing about — `unknown`, not blank, not zero.
' And a field only PARTLY present is `invalid`, never a shorter value: reading
' the first six digits of a truncated ten-digit amount gives a perfectly
' ordinary number a hundred times too small.
'
' One file carries non-ASCII: 94 codepoints in 95 bytes. The specification does
' not permit that either. It is reported and read.
'
' This is the step that could only have been written against files this project
' did not produce. Our own generator emits conforming files, so every fixture
' written here is 94 bytes and none of this was visible.
program main()
    load finio
    load finio_all

    reg = finio_all.registry()
    found = finio.scan(reg, "tests/finio/foreign/")

    print "record lengths actually seen:"
    for each path in found.files
        if contains(found.unknown, path) then
            continue
        end if
        doc = finio.read_file(reg, path, {})
        lens = {}
        for each rec in doc.records
            k = string(rec.byte_length)
            if has(lens, k) then
                lens[k] = lens[k] + 1
            else
                lens[k] = 1
            end if
        end for
        shape = []
        for each k in keys(lens)
            append(shape, k + " x" + string(lens[k]))
        end for
        flag = "   "
        if count(lens) > 1 then
            flag = "-> "
        end if
        print (flag + file_name(path) + "   " + join(shape, ", "))
    end for
    print ""

    ' Look at one short record closely. The file control record here is 55
    ' bytes where the format says 94 — and the fields inside those 55 bytes are
    ' still right.
    doc = finio.read_file(reg, "tests/finio/foreign/ppd-debit.ach", {})
    for each rec in doc.records
        if rec.kind = "file_control" then
            print ("ppd-debit.ach, the " + rec.kind + " record: "
                   + string(rec.byte_length) + " bytes where the format says 94")
            for each concept in keys(rec.fields)
                fld = rec.fields[concept]
                shown = fld.status + "  " + string(fld.value)
                if has(fld, "why") then
                    shown = shown + "  (" + fld.why + ")"
                end if
                print ("  " + concept + ": " + shown)
            end for
        end if
    end for
    print ""
    print "The fields the record actually carries read correctly. The ones past"
    print "its end are `unknown` — the source said nothing — rather than blank"
    print "or zero, which are claims the file never made."
end program
```

<!--OUT:03_files_the_spec_does_not_permit-->

```
record lengths actually seen:
-> 20110729A-invalid.ach   93 x1, 94 x292
   20110805A.ach   94 x93
-> 20180713-IAT.ach   94 x29, 55 x1
-> FISERV-ZEROFILE.ach   69 x1, 55 x1, 94 x8
   NACHA_SAMPLE_TEL_REVERSAL.ach   94 x10
   adv.ach   94 x10
   cor-example.ach   94 x10
-> extended-ascii.ach   94 x19, 95 x1
   gl-debit.ach   94 x10
-> ppd-debit.ach   75 x1, 94 x8, 55 x1

ppd-debit.ach, the file_control record: 55 bytes where the format says 94
  record_type: ok  9
  batch_count: ok  1
  block_count: ok  1
  entry_addenda_count: ok  1
  entry_hash: ok  23138010
  total_debit_amount: ok  1000000.00
  total_credit_amount: ok  0.00
  reserved: unknown  unknown  (the record ends at byte 55, before this field begins)

The fields the record actually carries read correctly. The ones past
its end are `unknown` — the source said nothing — rather than blank
or zero, which are claims the file never made.
```

---

## Step 4 — Count what you could not explain

Step 2 grouped issues by code. That is a report about these ten files. §13 asks
for something else: a signal that a **format** has moved, which is a claim about
the world and needs counting across time rather than across one drop.

An adapter that meets a transaction code it holds no direction for is not
broken. Once is a curiosity. Three hundred times across last quarter is stronger
evidence that a revision happened than any watch list, and it names the field to
go and read about.

Two rules make this safe to keep.

An observation records **a token and a location, never content**, and it is
enforced rather than asked for: a `detail` longer than 64 bytes is refused,
because a whole record passed as a "token" is a customer record in a log. That
is the one mistake this log could make that would matter.

And **a check produces work, never a patch.** A maintenance process that
modified an adapter would silently change how a file written in 2019 is read.
The mechanism detects and never updates, and a source tripwire enforces that.

<!--CODE:04_counting_what_you_cannot_explain-->

```basic
' Step 4 — Count what you could not explain.
'
' Step 2 grouped issues by code. That is a report about these ten files. §13
' asks for something else: a signal that a FORMAT has moved, which is a claim
' about the world and needs counting across time rather than across one drop.
'
' An adapter that meets a transaction code it holds no direction for is not
' broken. Once is a curiosity. Three hundred times across last quarter is
' stronger evidence that a revision happened than any watch list, and it names
' the field to go and read about.
'
' The rule that makes this safe to keep: an observation records A TOKEN AND A
' LOCATION, never content, and it is enforced. A `detail` longer than 64 bytes
' is refused, because a whole record passed as a "token" is a customer record
' in a log — and that is the one mistake this log could make that would matter.
program main()
    load finio
    load finio_all
    load finio_registry
    load finio_watch

    reg = finio_all.registry()
    found = finio.scan(reg, "tests/finio/foreign/")
    today = "2026-09-16"

    ' The three conformance codes that mean "this adapter did not understand
    ' something", as distinct from "this file's arithmetic is wrong".
    unexplained = [ "unknown_transaction_code", "unsupported_batch_kind", "non_ascii" ]

    log = {}
    for each path in found.files
        if contains(found.unknown, path) then
            continue
        end if
        doc = finio.read_file(reg, path, {})
        for each i in finio.validate(reg, doc)
            if contains(unexplained, i.code) then
                kind = "unknown_code"
                if i.code = "non_ascii" then
                    kind = "unexpected_length"
                end if
                log = finio_watch.observe(log,
                    { adapter: doc.adapter, revision: string(doc.revision),
                      kind: kind,
                      detail: i.code + " " + string(i.found),
                      where: file_name(path),
                      seen: today })
            end if
        end for
    end for

    print "what this adapter could not explain, across the whole drop:"
    for each o in finio_watch.observations_for(log, "aba.nacha")
        print ("  " + o.detail + "   seen " + string(o.occurrences)
               + "x in " + join(o.where, ", "))
    end for
    print ""

    ' The two signals together. §13: "An adapter with old evidence and no
    ' observations may be perfectly healthy — a stable format simply is not
    ' moving. An adapter with recent evidence and a rising observation count is
    ' the interesting case, and only the two together say so."
    entries = finio_registry.all(finio_all.adapters())
    print "the review queue a year from now, ranked:"
    for each q in finio_watch.review_queue(entries, log, "2027-10-01")
        if q.occurrences > 0 then
            print ("  " + q.id + "  rank " + string(q.rank))
            print ("    " + q.why)
        end if
    end for
    print ""
    print "Nothing here modifies an adapter. A check produces WORK, never a"
    print "patch: a process that edited the reader would silently change how a"
    print "file written in 2019 is read, and a source tripwire enforces that."
end program
```

<!--OUT:04_counting_what_you_cannot_explain-->

```
what this adapter could not explain, across the whole drop:
  unsupported_batch_kind 280   seen 1x in adv.ach
  unknown_transaction_code 81   seen 1x in adv.ach
  unknown_transaction_code 82   seen 1x in adv.ach
  unknown_transaction_code 21   seen 1x in cor-example.ach
  non_ascii 95   seen 1x in extended-ascii.ach

the review queue a year from now, ranked:
  aba.nacha  rank 7
    review is due, and 5 kind(s) of thing this adapter could not explain have been seen 5 time(s)

Nothing here modifies an adapter. A check produces WORK, never a
patch: a process that edited the reader would silently change how a
file written in 2019 is read, and a source tripwire enforces that.
```

---

## Step 5 — Change one field and send it back

The last thing an operations team does with a file is edit one field and return
it. A discretionary-data field is wrong; a name was transcribed badly; a trace
number needs correcting.

This is where §17's **byte fidelity** earns its place. `finio_nacha.write_doc`
is *re-emission, not origination*: it writes back the bytes it read, record by
record, with the terminator each record actually carried. A file that arrived
blocked goes back blocked; one with CRLF goes back with CRLF.

Nothing is normalised — because "normalised" means "different from what the
counterparty sent", and the counterparty's system is the one that has to read
it. The first line of the output is that claim checked: read the file, write it
straight back out, and compare to the bytes that arrived.

`set_field` **refuses rather than truncates**. A fixed-width field is exactly as
wide as it is, so a value of the wrong length is a mistake to report, never
something to pad or cut silently — a cut account number is still a plausible
account number.

And note `records that changed: 1`. A gBASIC record is a value, so `set_field`
cannot reach into the caller's document even by accident; the original is
untouched and exactly one record differs between the two. That is the property
an operations team actually needs, because a counterparty diffing the two files
must see one line change and no others.

<!--CODE:05_sending_one_back-->

```basic
' Step 5 — Changing one field and sending the file back.
'
' The last thing an operations team does with a file is edit one field in it
' and return it. A discretionary-data field is wrong; a name was transcribed
' badly; a trace number needs correcting.
'
' This is where §17's byte fidelity earns its place. `finio_nacha.write_doc` is
' RE-EMISSION, NOT ORIGINATION: it writes back the bytes it read, record by
' record, with the terminator each record actually carried. A file that arrived
' blocked goes back blocked; one with CRLF goes back with CRLF. Nothing is
' normalised, because "normalised" means "different from what the counterparty
' sent", and the counterparty's system is the one that has to read it.
'
' `set_field` REFUSES rather than truncates. A fixed-width field is exactly as
' wide as it is, so a value of the wrong length is a mistake to report — never
' something to pad or cut silently, because a cut account number is still a
' plausible account number.
program main()
    load finio
    load finio_all
    load finio_nacha

    reg = finio_all.registry()
    path = "tests/finio/nacha/payroll.ach"
    f {file}= path
    original = read(f)

    doc = finio.read_file(reg, path, {})

    ' First: read it and write it straight back out, untouched.
    same = finio_nacha.write_doc(doc)
    print ("read and re-emitted unchanged: byte-identical to the file that "
           + "arrived: " + string(same.text = original))
    print ""

    ' Now change one 22-byte name field. Exactly 22 bytes, padded by us.
    before = doc.records[2].fields.individual_name
    print ("record 2 individual_name before: [" + before.raw + "]")

    fixed = finio_nacha.set_field(doc, 2, "individual_name",
                                  "ALICE MERCER-OKONJO   ")
    after = fixed.records[2].fields.individual_name
    print ("record 2 individual_name after:  [" + after.raw + "]")
    print ("                         value:  " + string(after.value))
    print ""

    out = finio_nacha.write_doc(fixed)
    print ("the emitted file is the same length: "
           + string(byte_count(out.text) = byte_count(original)))
    print ("and differs from the original:       "
           + string(out.text != original))

    ' Only that record moved. Everything before and after it is untouched —
    ' which is the property an operations team actually needs, because a
    ' counterparty diffing the two files must see one line change.
    changed = 0
    i = 0
    while i < count(doc.records)
        if doc.records[i].raw != fixed.records[i].raw then
            changed = changed + 1
        end if
        i = i + 1
    end while
    print ("records that changed:                " + string(changed))
    print ""

    ' And a value of the wrong width is refused, not padded.
    on error goto next
    bad = finio_nacha.set_field(doc, 2, "individual_name", "ALICE MERCER")
    if error then
        print ("a 12-byte value for a 22-byte field:")
        print ("  " + error.message)
        error.clear()
    end if
    on error stop
end program
```

<!--OUT:05_sending_one_back-->

```
read and re-emitted unchanged: byte-identical to the file that arrived: true

record 2 individual_name before: [ALICE MERCER          ]
record 2 individual_name after:  [ALICE MERCER-OKONJO   ]
                         value:  ALICE MERCER-OKONJO

the emitted file is the same length: true
and differs from the original:       true
records that changed:                1

a 12-byte value for a 22-byte field:
  finio_nacha.set_field: 'individual_name' is 22 bytes and the value given is 12 -- pad or refuse, never truncate
```

---

## Where to go next

- **Task-oriented recipes** — [finio_cookbook.md](finio_cookbook.md): ten
  recipes covering provenance, `unknown` against `invalid`, framing, five
  formats through one call, what the reader did not claim, writing and refusing,
  the acquisition queue, and the maintenance mechanism.
- **The other four adapters** — this tutorial is entirely ACH, because a single
  worked problem is clearer than five shallow ones. Recipe 6 of the cookbook
  reads a BAI2 statement, two ISO 20022 messages and an OFX download through the
  same call.
- **Writing a file you will send** — recipe 8. Outbound is a different problem
  from inbound, and §16 refuses by default.
- **What this does not cover** — `finio_camt` has never been run against a
  statement a bank produced. See the cookbook's closing section.
