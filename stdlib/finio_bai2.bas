' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' finio_bai2 -- BAI2 cash management balance reporting, as a finio adapter.
' See docs/financial_adapters_design.md §20.
'
' THE THIRD STRUCTURAL SHAPE, and chosen for that: NACHA is fixed-width, camt
' is hierarchical XML, and this is DELIMITED AND VARIABLE-LENGTH with a logical
' record that can span several physical ones. It is the first caller of §4's
' `delimited` location kind, which existed in the enum with nothing using it.
'
' IT IS ALSO THE FORMAT WITH THE MOST AWKWARD FRAMING IN THIS TREE, and every
' bit of that was learned from files written by somebody else before a line of
' this was written:
'
'   THE RECORD SEPARATOR IS A SLASH, NOT A NEWLINE. A physical line may carry
'   two whole records -- `01,...,2/   02,...,2/` -- and a newline-per-record
'   reader silently merges them.
'
'   AND A RECORD WHOSE LAST FIELD IS FREE TEXT OFTEN HAS NO TERMINATOR AT ALL,
'   running to the end of the line. In one real sample 102 of 116 records end
'   without a slash.
'
'   AND THAT TEXT CAN CONTAIN SLASHES. Splitting on every slash shatters those
'   records into fragments -- which is exactly the defect moov-io's
'   `sample5-issue113.txt` is named for. So the generic source layer splits
'   MECHANICALLY at a terminator or a newline, and THIS ADAPTER FOLDS BACK any
'   fragment that does not begin with a record code it knows, restoring the
'   slash it was split on. A fragment that is not a record is part of the
'   previous record's text, which is a fact about BAI2 and belongs here rather
'   than in `finio`.
'
'   THE FOLD CAN BE WRONG and the limitation is stated rather than hidden: text
'   containing something that looks exactly like a record start -- `/16,` --
'   would be split. It is reported as a count so a reader can see how much
'   folding a file needed.
'
' WHAT IT KNOWS AND HOW (Axiom 11). The BAI format specifications are no longer
' charged for, and several banks publish complete field-level implementation
' guides openly; the registry entry cites them. The structure and the
' three-level control totals below were checked against the SPECIFICATION'S OWN
' WORKED EXAMPLE, which moov-io ships as `spec-section3.txt` and whose
' arithmetic can be verified by hand: 500000 + 70000000 + 1500000 = 72000000,
' which is what its account trailer declares.

library finio_bai2

load finio from "finio.bas"

function record_codes()
    return [ "01", "02", "03", "16", "49", "88", "98", "99" ]
end function

function record_kind(code)
    if code = "01" then
        return "file_header"
    end if
    if code = "02" then
        return "group_header"
    end if
    if code = "03" then
        return "account_identifier"
    end if
    if code = "16" then
        return "transaction_detail"
    end if
    if code = "49" then
        return "account_trailer"
    end if
    if code = "88" then
        return "continuation"
    end if
    if code = "98" then
        return "group_trailer"
    end if
    if code = "99" then
        return "file_trailer"
    end if
    return "unknown"
end function

' --- §7 + Axiom 10: recognition -------------------------------------------

function recognise(text)
    reasons = []
    if byte_count(text) = 0 then
        return { classification: "unknown", reasons: [ "the source is empty" ] }
    end if
    src = finio.open_text(text, { format: "bai2", revision: "2",
                                  framing: "terminated", record_terminator: "/" })
    recs = _fold(src.records, src.terminators)
    if count(recs) = 0 then
        return { classification: "unknown", reasons: [ "the source holds no records" ] }
    end if
    first = _code_of(recs[0].text)
    if first != "01" then
        return { classification: "unknown",
                 framing: "terminated", record_terminator: "/",
                 reasons: [ "the first record is code '" + first + "', not a file header (01)" ] }
    end if
    known = 0
    unknown_codes = 0
    for each r in recs
        if contains(record_codes(), _code_of(r.text)) then
            known = known + 1
        else
            unknown_codes = unknown_codes + 1
        end if
    end for
    append(reasons, string(count(recs)) + " records, " + string(known) + " carrying a BAI2 record code")
    if unknown_codes > 0 then
        append(reasons, string(unknown_codes) + " record(s) carry a code this format does not define")
    end if
    last = _code_of(recs[count(recs) - 1].text)
    if last != "99" then
        append(reasons, "the file does not end with a file trailer (99); the last record is '" + last + "'")
        return { classification: "possible", framing: "terminated", record_terminator: "/",
                 reasons: reasons }
    end if
    append(reasons, "and it ends with a file trailer (99)")
    ' THE VERSION FIELD IS THE FINGERPRINT. Field 9 of the file header is the
    ' version number, and `2` is what makes this BAI2 rather than the original
    ' BAI. A file that is structurally right and declares something else is
    ' still recognisable and is not this revision.
    fields = _split_fields(recs[0].text)
    ver = ""
    if count(fields) > 8 then
        ver = trim(fields[8])
    end if
    if ver = "2" then
        append(reasons, "and the file header declares version 2")
        if unknown_codes > 0 then
            return { classification: "strong", revision: "2",
                     framing: "terminated", record_terminator: "/", reasons: reasons }
        end if
        return { classification: "exact", revision: "2",
                 framing: "terminated", record_terminator: "/", reasons: reasons }
    end if
    append(reasons, "but the file header declares version '" + ver + "', not 2")
    return { classification: "strong", framing: "terminated", record_terminator: "/",
             reasons: reasons }
end function

function _code_of(t)
    at = byte_find(t, ",", 0)
    if is_nothing(at) then
        return trim(t)
    end if
    return trim(byte_slice(t, 0, at))
end function

' THE FOLD. `finio.open_text` split mechanically at every slash; a fragment
' whose first field is not a record code was never a record, so it is part of
' the previous record's text and the slash it was split on is restored.
function _fold(raw, terminators)
    out = []
    i = 0
    for each piece in raw
        c = _code_of(piece)
        ' ONLY A PIECE THAT CAME FROM A SLASH SPLIT CAN BE A FRAGMENT. One that
        ' begins a LINE is a record in its own right however odd its code --
        ' one real sample carries a line of fifteen 1s, which is block padding
        ' some producers emit, and folding that onto the previous transaction
        ' would append it to that transaction's TEXT and lose a record from the
        ' count the file's own trailer states. The terminator that preceded the
        ' piece says which case it is, and `open_text` already recorded it.
        after_slash = false
        if i > 0 then
            if i - 1 < count(terminators) then
                after_slash = terminators[i - 1] = "/"
            end if
        end if
        if count(out) > 0 and after_slash and not contains(record_codes(), c) then
            k = count(out) - 1
            out[k].text = out[k].text + "/" + piece
            out[k].folded = out[k].folded + 1
        else
            append(out, { text: piece, folded: 0 })
        end if
        i = i + 1
    end for
    return out
end function

' Fields are comma-separated. The LAST field of a text-bearing record may
' itself contain commas, which is why the field list is produced whole and the
' text is taken as "everything from field n onward" rather than as field n.
function _split_fields(t)
    out = []
    n = byte_count(t)
    start = 0
    i = 0
    while i <= n
        if i = n then
            append(out, byte_slice(t, start, i - start))
            i = i + 1
        else
            if byte_slice(t, i, 1) = "," then
                append(out, byte_slice(t, start, i - start))
                start = i + 1
            end if
            i = i + 1
        end if
    end while
    return out
end function

function _rest_from(t, index)
    ' Everything from field `index` onward, commas included -- the text field.
    n = byte_count(t)
    seen = 0
    i = 0
    while i < n
        if byte_slice(t, i, 1) = "," then
            seen = seen + 1
            if seen = index then
                return byte_slice(t, i + 1, n - i - 1)
            end if
        end if
        i = i + 1
    end while
    return ""
end function

' --- reading ---------------------------------------------------------------
'
' §15: READING PRESERVES AND EXPLAINS. A record whose code this format does not
' define is kept with kind "unknown" and its text intact -- one real sample
' carries a line of fifteen 1s, which is block padding some producers emit --
' and a file whose control totals disagree reads exactly like one whose totals
' agree. The disagreement is `validate`'s to report.
'
' LOCATIONS ARE `delimited`: a row and a column, where the row is the LOGICAL
' record's index and the column is the field's. The physical line is carried
' beside it as the optional `line`, because two logical records can share one
' line and it is the line a person opens an editor to.

function read_source(src, revision)
    folded = _fold(src.records, src.terminators)
    records = []
    loss = []
    i = 0
    for each fr in folded
        code = _code_of(fr.text)
        kind = record_kind(code)
        append(records, { index: i, kind: kind, code: code, raw: fr.text,
                          folded: fr.folded, continues: unknown, fields: {} })
        if kind = "unknown" then
            append(loss, finio.loss_note("uninterpreted",
                { record: i, raw: left(fr.text, 40),
                  why: "record code '" + code + "' is not one this format defines; the text is kept" }))
        end if
        i = i + 1
    end for
    ' A CONTINUATION EXTENDS THE PREVIOUS RECORD'S FIELD LIST, and getting that
    ' wrong is the single largest mistake available in this format. An `88` is
    ' not "more text": its fields are APPENDED to the record it continues, so an
    ' account identifier's summary groups can begin on the `03` and finish two
    ' continuations later -- and one real sample ends an `03` on a bare type
    ' code with its amount arriving on the next record.
    '
    ' THE COST OF GETTING IT WRONG IS AN ORDINARY-LOOKING TOTAL. Read as text,
    ' sample2's first account totals 3,280,000 where its own trailer says
    ' 9,150,000, and the four summary amounts on its continuation are simply
    ' absent. Verified by hand against that file: 4350000 + 2830000 + 1020000 +
    ' 500000 + 450000 = 9150000.
    k = 0
    while k < count(records)
        r = records[k]
        if r.kind != "continuation" and r.kind != "unknown" then
            fields = _split_fields(r.raw)
            j = k + 1
            while j < count(records) and records[j].kind = "continuation"
                more = _split_fields(records[j].raw)
                m = 1
                while m < count(more)
                    append(fields, more[m])
                    m = m + 1
                end while
                records[j].continues = k
                j = j + 1
            end while
            records[k].fields = _fields_from(r.kind, fields, r.raw, k)
        end if
        k = k + 1
    end while
    return { records: records, entities: _entities(records), loss: loss }
end function

function _at(row, column)
    return finio.location("delimited", { row: row, column: column })
end function

function _text_field(fields, idx, row)
    if idx >= count(fields) then
        return { status: "unknown", value: unknown, raw: unknown, location: _at(row, idx),
                 why: "the record has only " + string(count(fields)) + " fields" }
    end if
    raw = fields[idx]
    body = trim(raw)
    if byte_count(body) = 0 then
        return { status: "unknown", value: unknown, raw: raw, location: _at(row, idx) }
    end if
    return { status: "ok", value: body, raw: raw, location: _at(row, idx) }
end function

' AMOUNTS ARE MINOR UNITS WITH NO DECIMAL POINT, exactly as in NACHA -- and
' unlike NACHA they may be NEGATIVE, which real files carry (an opening ledger
' of -3500 appears in one sample). A leading minus is part of the number and
' anything else is `invalid`, never a partial read.
function _amount_field(fields, idx, row)
    f = _text_field(fields, idx, row)
    if f.status != "ok" then
        return f
    end if
    body = f.value
    neg = false
    ' BOTH SIGNS ARE WRITTEN IN REAL FILES. `+4350000` appears in the
    ' specification's own sample set; reading it as non-numeric made an entire
    ' account total "unreadable" and lost 4.35 million from a control total.
    if byte_slice(body, 0, 1) = "-" then
        neg = true
        body = byte_slice(body, 1, byte_count(body) - 1)
    else
        if byte_slice(body, 0, 1) = "+" then
            body = byte_slice(body, 1, byte_count(body) - 1)
        end if
    end if
    if not _all_digits(body) then
        return { status: "invalid", value: unknown, raw: f.raw, location: f.location,
                 why: "'" + f.value + "' is not an amount in minor units" }
    end if
    cents = number(body)
    if neg then
        cents = 0 - cents
    end if
    return { status: "ok", value: cents, raw: f.raw, location: f.location, cents: cents }
end function

function _count_field(fields, idx, row)
    f = _text_field(fields, idx, row)
    if f.status != "ok" then
        return f
    end if
    if not _all_digits(f.value) then
        return { status: "invalid", value: unknown, raw: f.raw, location: f.location,
                 why: "'" + f.value + "' is not a count" }
    end if
    return { status: "ok", value: number(f.value), raw: f.raw, location: f.location }
end function

' HOW MANY FIELDS THE FUNDS TYPE ITSELF CARRIES, written once because the
' rule is the same wherever a funds type appears -- in a transaction record and
' inside an account summary group. Two copies of it would drift, and the copy
' that drifted would produce a displaced field rather than an error.
'   S  three availability amounts (immediate, one day, two or more)
'   V  a value date and a value time
'   D  a count, then that many (days, amount) pairs
' Anything else carries nothing.
function _funds_extra(f, idx, row)
    ft = _text_field(f, idx, row)
    if ft.status != "ok" then
        return 0
    end if
    if ft.value = "S" then
        return 3
    end if
    if ft.value = "V" then
        return 2
    end if
    if ft.value = "D" then
        nd = _count_field(f, idx + 1, row)
        if nd.status = "ok" then
            return 1 + nd.value * 2
        end if
        return 1
    end if
    return 0
end function

function _all_digits(s)
    n = byte_count(s)
    if n = 0 then
        return false
    end if
    i = 0
    while i < n
        c = byte_slice(s, i, 1)
        if c < "0" or c > "9" then
            return false
        end if
        i = i + 1
    end while
    return true
end function

function _fields_from(kind, f, t, row)
    if kind = "file_header" then
        return { sender_id: _text_field(f, 1, row),
                 receiver_id: _text_field(f, 2, row),
                 file_creation_date: _text_field(f, 3, row),
                 file_creation_time: _text_field(f, 4, row),
                 file_id: _text_field(f, 5, row),
                 physical_record_length: _count_field(f, 6, row),
                 block_size: _count_field(f, 7, row),
                 version_number: _text_field(f, 8, row) }
    end if
    if kind = "group_header" then
        return { ultimate_receiver_id: _text_field(f, 1, row),
                 originator_id: _text_field(f, 2, row),
                 group_status: _text_field(f, 3, row),
                 as_of_date: _text_field(f, 4, row),
                 as_of_time: _text_field(f, 5, row),
                 currency_code: _text_field(f, 6, row),
                 as_of_date_modifier: _text_field(f, 7, row) }
    end if
    if kind = "account_identifier" then
        ' THE SUMMARY IS A REPEATING GROUP OF FOUR, from field 3 on:
        ' (type_code, amount, item_count, funds_type). An all-empty group is an
        ' absent summary and not a zero one -- Axiom 7 at the level of a
        ' repetition rather than a field.
        ' A SUMMARY GROUP IS NOT FOUR FIELDS. It is (type, amount, item count,
        ' funds type) AND THEN WHATEVER THE FUNDS TYPE CARRIES -- the same rule
        ' that governs a transaction record, applied inside a repetition. Seen
        ' in the specification's own sample set: `100,000000000208500,00003,V,
        ' 060316,` is ONE group of six, and stepping four at a time lands the
        ' next group on the value date `060316`, which is then read as a type
        ' code with the field after it as its amount. Every later group is
        ' displaced, and the total that comes out is an ordinary number.
        summaries = []
        j = 3
        while j < count(f)
            tc = _text_field(f, j, row)
            ft = _text_field(f, j + 3, row)
            if tc.status = "ok" then
                append(summaries, { type_code: tc,
                                    amount: _amount_field(f, j + 1, row),
                                    item_count: _count_field(f, j + 2, row),
                                    funds_type: ft })
            end if
            j = j + 4 + _funds_extra(f, j + 3, row)
        end while
        return { customer_account_number: _text_field(f, 1, row),
                 currency_code: _text_field(f, 2, row),
                 summaries: summaries }
    end if
    if kind = "transaction_detail" then
        ' THE FUNDS TYPE CHANGES WHERE THE REST OF THE RECORD IS. `S` is
        ' followed by three availability amounts, `D` by a count and that many
        ' (days, amount) pairs, `V` by a value date and time -- and only then do
        ' the bank reference, customer reference and text begin. A reader that
        ' assumes a fixed layout takes an AVAILABILITY AMOUNT as the bank
        ' reference number, which is an ordinary-looking string in an ordinary
        ' place. Seen in the specification's own sample set:
        ' `16,115,450000,S,100000,200000,150000,,,/`.
        ft = _text_field(f, 3, row)
        after = 4 + _funds_extra(f, 3, row)
        return { type_code: _text_field(f, 1, row),
                 amount: _amount_field(f, 2, row),
                 funds_type: ft,
                 bank_reference_number: _text_field(f, after, row),
                 customer_reference_number: _text_field(f, after + 1, row),
                 ' THE TEXT IS EVERYTHING FROM ITS FIELD ON, commas included: it
                 ' is free text and real files put commas in it constantly.
                 ' Reading it as one field truncates at the first comma and
                 ' produces a plausible shorter description.
                 text: { status: "ok", value: trim(_rest_from(t, after + 2)),
                         raw: _rest_from(t, after + 2), location: _at(row, after + 2) } }
    end if
    if kind = "account_trailer" then
        return { account_control_total: _amount_field(f, 1, row),
                 number_of_records: _count_field(f, 2, row) }
    end if
    if kind = "group_trailer" then
        return { group_control_total: _amount_field(f, 1, row),
                 number_of_accounts: _count_field(f, 2, row),
                 number_of_records: _count_field(f, 3, row) }
    end if
    if kind = "file_trailer" then
        return { file_control_total: _amount_field(f, 1, row),
                 number_of_groups: _count_field(f, 2, row),
                 number_of_records: _count_field(f, 3, row) }
    end if
    if kind = "continuation" then
        return { text: { status: "ok", value: trim(_rest_from(t, 1)),
                         raw: _rest_from(t, 1), location: _at(row, 1) } }
    end if
    return {}
end function

' The hierarchy: file -> groups -> accounts -> transactions, as indices.
function _entities(records)
    header = unknown
    trailer = unknown
    groups = []
    g = unknown
    a = unknown
    for each r in records
        if r.kind = "file_header" then
            header = r.index
        end if
        if r.kind = "group_header" then
            g = { header: r.index, accounts: [], trailer: unknown }
        end if
        if r.kind = "account_identifier" then
            if not is_unknown(g) then
                a = { identifier: r.index, transactions: [], trailer: unknown }
            end if
        end if
        if r.kind = "transaction_detail" then
            if not is_unknown(a) then
                append(a.transactions, r.index)
            end if
        end if
        if r.kind = "account_trailer" then
            if not is_unknown(a) then
                a.trailer = r.index
                append(g.accounts, a)
                a = unknown
            end if
        end if
        if r.kind = "group_trailer" then
            if not is_unknown(g) then
                g.trailer = r.index
                append(groups, g)
                g = unknown
            end if
        end if
        if r.kind = "file_trailer" then
            trailer = r.index
        end if
    end for
    ' §15: a group or account whose trailer never arrived is KEPT, so the
    ' records inside it are still readable, and `validate` reports the absence.
    if not is_unknown(a) and not is_unknown(g) then
        append(g.accounts, a)
    end if
    if not is_unknown(g) then
        append(groups, g)
    end if
    return { header: header, groups: groups, trailer: trailer }
end function

' --- §15: validation -------------------------------------------------------
'
' A THREE-LEVEL CHECKSUM, which is the strongest self-checking property of the
' three formats in this tree. Each account trailer states the sum of its own
' account's amounts and how many records it spans; each group trailer sums its
' accounts; the file trailer sums its groups. Every one of those numbers was
' computed by whoever produced the file.
'
' CHECKED AGAINST THE SPECIFICATION'S OWN WORKED EXAMPLE, whose arithmetic can
' be verified by hand: 500000 + 70000000 (the account summary) + 1500000 (its
' one transaction) = 72000000, which is what its account trailer declares, and
' the record counts run 3, 5, 7 up the three levels.

function validate_doc(doc)
    issues = []
    recs = doc.records
    n = count(recs)
    ent = doc.entities
    if is_unknown(ent.header) then
        append(issues, { code: "missing_file_header", severity: "error",
                         message: "the file has no file header record (01)" })
    end if
    if is_unknown(ent.trailer) then
        append(issues, { code: "missing_file_trailer", severity: "error",
                         message: "the file has no file trailer record (99)" })
    end if
    for each r in recs
        if r.kind = "unknown" then
            append(issues, { code: "record_code", severity: "error", record: r.index,
                             message: ("record " + string(r.index) + " carries code '" + r.code
                                       + "', which this format does not define"),
                             found: r.code })
        end if
    end for
    file_total = 0
    file_records = 0
    for each g in ent.groups
        if is_unknown(g.trailer) then
            append(issues, { code: "missing_group_trailer", severity: "error",
                             record: g.header,
                             message: "the group beginning at record " + string(g.header) + " has no trailer (98)" })
        end if
        group_total = 0
        for each a in g.accounts
            t = account_total(recs, a)
            if is_unknown(a.trailer) then
                append(issues, { code: "missing_account_trailer", severity: "error",
                                 record: a.identifier,
                                 message: "the account beginning at record " + string(a.identifier) + " has no trailer (49)" })
            else
                c = recs[a.trailer].fields
                group_total = group_total + t.total
                append(issues, _compare(c, "account_control_total", t.total, a.trailer,
                    "account_control_total", "the account trailer's control total is"))
                append(issues, _compare(c, "number_of_records", _counted(recs, a.identifier, a.trailer), a.trailer,
                    "account_record_count", "the account trailer counts"))
            end if
        end for
        if not is_unknown(g.trailer) then
            gc = recs[g.trailer].fields
            file_total = file_total + group_total
            append(issues, _compare(gc, "group_control_total", group_total, g.trailer,
                "group_control_total", "the group trailer's control total is"))
            append(issues, _compare(gc, "number_of_accounts", count(g.accounts), g.trailer,
                "group_account_count", "the group trailer counts"))
            append(issues, _compare(gc, "number_of_records", _counted(recs, g.header, g.trailer), g.trailer,
                "group_record_count", "the group trailer counts"))
        end if
    end for
    if not is_unknown(ent.trailer) then
        fc = recs[ent.trailer].fields
        append(issues, _compare(fc, "file_control_total", file_total, ent.trailer,
            "file_control_total", "the file trailer's control total is"))
        append(issues, _compare(fc, "number_of_groups", count(ent.groups), ent.trailer,
            "file_group_count", "the file trailer counts"))
        append(issues, _compare(fc, "number_of_records", _counted(recs, 0, n - 1), ent.trailer,
            "file_record_count", "the file trailer counts"))
    end if
    return _flatten(issues)
end function

' THE ACCOUNT TOTAL IS THE SUMMARY PLUS THE TRANSACTIONS, which is the one
' piece of BAI2 arithmetic a reader can get wrong in a way that still looks
' like a number: counting only the transactions gives a total that is merely
' too small, and counting only the summary gives one that is merely too large.
' THE TRAILERS COUNT BAI2 RECORDS, and a line that is not one is not counted.
' One real sample carries a line of fifteen 1s -- block padding some producers
' emit -- and counting it made the account, group and file record counts all
' disagree: ONE cause reported FOUR times, three of them pointing away from it.
' The unrecognised record is still reported, once, by the check written for it.
' `first`/`last`, because `to` is a reserved word (`for i = 1 to n`) and `from`
' is one too -- the same wall `new`, `step` and `without` put up elsewhere.
function _counted(recs, first, last)
    n = 0
    i = first
    while i <= last and i < count(recs)
        if recs[i].kind != "unknown" then
            n = n + 1
        end if
        i = i + 1
    end while
    return n
end function

function account_total(recs, a)
    total = 0
    summed = 0
    unreadable = 0
    for each s in recs[a.identifier].fields.summaries
        if s.amount.status = "ok" then
            total = total + s.amount.cents
            summed = summed + 1
        else
            if s.amount.status = "invalid" then
                unreadable = unreadable + 1
            end if
        end if
    end for
    for each ti in a.transactions
        amt = recs[ti].fields.amount
        if amt.status = "ok" then
            total = total + amt.cents
            summed = summed + 1
        else
            if amt.status = "invalid" then
                unreadable = unreadable + 1
            end if
        end if
    end for
    return { total: total, amounts: summed, unreadable: unreadable }
end function

function _compare(fields, concept, want, record_index, code, phrase)
    got = fields[concept]
    if got.status != "ok" then
        return [ { code: code, severity: "error", record: record_index, concept: concept,
                   message: phrase + " unreadable; the records give " + string(want),
                   expected: string(want), found: string(got.raw) } ]
    end if
    if got.value = want then
        return []
    end if
    return [ { code: code, severity: "error", record: record_index, concept: concept,
               message: (phrase + " " + string(got.value) + "; the records give " + string(want)
                         + " (" + finio.describe_location(got.location) + ")"),
               expected: string(want), found: string(got.value) } ]
end function

function _flatten(items)
    out = []
    for each x in items
        if type(x) = "array" then
            for each y in x
                append(out, y)
            end for
        else
            append(out, x)
        end if
    end for
    return out
end function

' --- §16 + §17: writing ----------------------------------------------------
'
' BYTE FIDELITY, RECONSTRUCTED. Each record's text is retained and re-emitted
' with the terminator it actually carried -- which for BAI2 is a slash, a
' newline, or NOTHING AT ALL where a text field ran to the end of the file.
' A record folded back together carries its slashes inside its text, so it is
' re-emitted whole.

function write_doc(doc)
    out = ""
    src = doc.source
    i = 0
    for each r in doc.records
        term = ""
        if i < count(src.terminators) then
            term = src.terminators[i]
        end if
        out = out + r.raw + term
        i = i + 1
    end for
    return { text: out, loss: [] }
end function

' --- the adapter -----------------------------------------------------------

function registry_entry()
    return { id: "bai2",
             name: "BAI2 cash management balance reporting",
             family: "cash management",
             domain: "US bank balance and transaction reporting to corporate customers",
             authority: "originally the Bank Administration Institute; the technical reference is now associated with ASC X9",
             description: "Record-oriented and comma-delimited, hierarchical by file, group, account and transaction, with a numeric type-code vocabulary identifying each balance and activity kind, and a three-level control total.",
             representation: "delimited text, variable-length records terminated by a slash, with continuation records",
             transport: "file, delivered by the account servicer",
             known_revisions: [ "2" ],
             specification_sources: [
               { source_type: "standards_body",
                 source_url_or_reference: "ASC X9 / BAI, Cash Management Balance Reporting Specifications Version 2 Technical Reference Manual (preview: https://webstore.ansi.org/preview-pages/ASCX9/preview_X9+BAI+Version+2-2009.pdf)",
                 date_retrieved: "2026-09-15",
                 note: "BAI no longer charges for the format specifications. The PREVIEW page was seen; the full manual was not retrieved." },
               { source_type: "implementation_guide",
                 source_url_or_reference: "Bank implementation guides published openly, e.g. HSBC (https://www.hsbcnet.com/-/media/hsbcnet/client-transition/bai2-ir-specs.pdf), East West Bank, Scotiabank, Bendigo Bank",
                 date_retrieved: "2026-09-15",
                 note: "several independent, freely downloadable field-level guides that agree" },
               { source_type: "public_sample",
                 source_url_or_reference: "the specification's own worked example, shipped by moov-io/bai2 as test/testdata/spec-section3.txt",
                 date_retrieved: "2026-09-15",
                 note: "SEVEN RECORDS WHOSE ARITHMETIC CAN BE CHECKED BY HAND: 500000 + 70000000 + 1500000 = 72000000, which is what its account trailer declares, and record counts of 3, 5 and 7 up the three levels. The structure and the control-total rule in this adapter were checked against it." } ],
             acquisition_class: "OPEN",
             spec_public: true,
             spec_acquisition_method: "Free. The format specifications are no longer charged for, and multiple banks publish complete field-level implementation guides.",
             implementation_allowed: true,
             spec_redistribution_allowed: false,
             sample_redistribution_allowed: false,
             state: "researched",
             recognition_status: "implemented",
             read_status: "implemented",
             write_status: "re-emission only; this adapter does not originate a report",
             validation_status: "implemented",
             test_vectors: [
               { name: "tests/finio/bai2/*.bai",
                 source_url_or_reference: "generated by tools/make_bai2_fixture.py in this repository",
                 date_retrieved: "2026-09-15",
                 licence: "Apache-2.0 (this project)",
                 usage: "permitted", redistribution: "permitted",
                 note: "written here, from this project's model of the format" },
               { name: "tests/finio/foreign_bai2/*.txt",
                 source_url_or_reference: "https://github.com/moov-io/bai2 test/testdata -- files produced by a DIFFERENT implementation, plus the specification's own worked example",
                 date_retrieved: "2026-09-15",
                 licence: "Apache-2.0, Copyright The Moov Authors",
                 usage: "permitted", redistribution: "permitted",
                 note: "redistributed unmodified under section 4 with the licence alongside. READ BEFORE THIS ADAPTER WAS WRITTEN, which is why the framing is right: they showed that the record separator is a slash rather than a newline, that a text-bearing record often has no terminator at all, and that text fields contain slashes." } ],
             known_variants: [
               "two whole records packed onto one physical line, separated by the slash terminator",
               "text fields containing the slash terminator, which shatters a reader that splits on every slash",
               "block padding: a record of fifteen 1s appears in one real sample",
               "a continuation record using a colon instead of a comma after the code" ],
             known_extensions: [],
             maintenance_priority: "periodic",
             watch_sources: [
               { kind: "registry_page",
                 reference: "https://webstore.ansi.org/preview-pages/ASCX9/preview_X9+BAI+Version+2-2009.pdf",
                 watching: "a version beyond 2 appearing, or the specification ceasing to be free" } ],
             last_reviewed: "2026-09-15",
             next_review_due: "2027-09-15" }
end function

function adapter()
    return finio.adapter({
        id: "bai2",
        revisions: [ "2" ],
        recognise: recognise,
        read: read_source,
        validate: validate_doc,
        write: write_doc,
        registry_entry: registry_entry(),
        byte_fidelity: true,
        semantic_fidelity: true })
end function
end library
