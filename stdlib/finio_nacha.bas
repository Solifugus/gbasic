' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' finio_nacha -- the ACH file format, as a finio adapter.
' See docs/financial_adapters_design.md (Phase 1/2) and §20's proving set.
'
' WHY THIS FORMAT FIRST. It is fixed-width and record-oriented, which is the
' shape Phase 0's measurement was taken against, so the framework is being
' exercised on the representation whose provenance is most expensive. It is
' also the one an American business is most likely to actually hold. But the
' reason that decided it is this: A NACHA FILE CHECKS ITSELF. Every batch ends
' with a control record stating the entry count, a hash of the routing numbers
' and the debit and credit totals, and the file ends with a control record
' stating the same things across batches. Those numbers were computed by
' WHOEVER PRODUCED THE FILE, so a reader can be held to arithmetic the reader
' did not supply -- which is the difference between a test and a transcript,
' and it is the same property that makes `accounting`'s balance identity and
' `credit`'s reconciliation good tests rather than goldens.
'
' WHAT THIS ADAPTER KNOWS AND HOW IT KNOWS IT (Axiom 11). The record layouts
' below are the ACH file layouts published in the ACH origination guides that
' US financial institutions issue to their own originating customers; those
' documents are public and describe the same 94-character records. THE NACHA
' OPERATING RULES THEMSELVES ARE A PAID PUBLICATION AND I DO NOT HOLD A COPY.
' That is why the registry entry below says `researched` and not `verified`,
' why `acquisition_class` is DE_FACTO rather than OPEN, and why §8's statement
' about `ldap` applies here word for word: this adapter has never met a file
' produced by a bank. Everything it has been run against was written here.
'
' WHAT IT DELIBERATELY DOES NOT DO. It does not decide whether a file may be
' transmitted, whether a return is actionable, or whether a mismatch is serious
' (Axiom 9 and §18 -- those are the consumer's). It does not claim to
' distinguish rule-book years: the record layout has outlived twenty of them
' and A FILE DOES NOT SAY WHICH YEAR'S RULES PRODUCED IT, so `recognise`
' reports the revision it cannot determine rather than naming one, which is
' Axiom 6 at the revision level.

library finio_nacha

load finio from "finio.bas"

' --- the record layouts ----------------------------------------------------
'
' Offsets are 0-BASED BYTE offsets, which is what `finio` counts in. Published
' ACH guides number the same positions from 1, so a layout transcribed straight
' out of one is off by one everywhere -- an ordinary-looking raw field from the
' neighbouring column, which is the defect finio's own location tier exists to
' catch. Every layout here totals exactly 94, and the suite asserts that with
' `finio.coverage` rather than trusting the addition below.

function layout_file_header()
    return finio.layout([
        { concept: "record_type",                offset: 0,  length: 1 },
        { concept: "priority_code",              offset: 1,  length: 2 },
        { concept: "immediate_destination",      offset: 3,  length: 10 },
        { concept: "immediate_origin",           offset: 13, length: 10 },
        { concept: "file_creation_date",         offset: 23, length: 6 },
        { concept: "file_creation_time",         offset: 29, length: 4 },
        { concept: "file_id_modifier",           offset: 33, length: 1 },
        { concept: "record_size",                offset: 34, length: 3 },
        { concept: "blocking_factor",            offset: 37, length: 2 },
        { concept: "format_code",                offset: 39, length: 1 },
        { concept: "immediate_destination_name", offset: 40, length: 23 },
        { concept: "immediate_origin_name",      offset: 63, length: 23 },
        { concept: "reference_code",             offset: 86, length: 8 } ])
end function

function layout_batch_header()
    return finio.layout([
        { concept: "record_type",                   offset: 0,  length: 1 },
        { concept: "service_class_code",            offset: 1,  length: 3 },
        { concept: "company_name",                  offset: 4,  length: 16 },
        { concept: "company_discretionary_data",    offset: 20, length: 20 },
        { concept: "company_identification",        offset: 40, length: 10 },
        { concept: "standard_entry_class_code",     offset: 50, length: 3 },
        { concept: "company_entry_description",     offset: 53, length: 10 },
        { concept: "company_descriptive_date",      offset: 63, length: 6 },
        { concept: "effective_entry_date",          offset: 69, length: 6 },
        { concept: "settlement_date",               offset: 75, length: 3 },
        { concept: "originator_status_code",        offset: 78, length: 1 },
        { concept: "originating_dfi_identification", offset: 79, length: 8 },
        { concept: "batch_number",                  offset: 87, length: 7 } ])
end function

function layout_entry_detail()
    return finio.layout([
        { concept: "record_type",                      offset: 0,  length: 1 },
        { concept: "transaction_code",                 offset: 1,  length: 2 },
        { concept: "receiving_dfi_identification",     offset: 3,  length: 8 },
        { concept: "check_digit",                      offset: 11, length: 1 },
        { concept: "dfi_account_number",               offset: 12, length: 17 },
        { concept: "amount",                           offset: 29, length: 10 },
        { concept: "individual_identification_number", offset: 39, length: 15 },
        { concept: "individual_name",                  offset: 54, length: 22 },
        { concept: "discretionary_data",               offset: 76, length: 2 },
        { concept: "addenda_record_indicator",         offset: 78, length: 1 },
        { concept: "trace_number",                     offset: 79, length: 15 } ])
end function

function layout_addenda()
    return finio.layout([
        { concept: "record_type",                  offset: 0,  length: 1 },
        { concept: "addenda_type_code",            offset: 1,  length: 2 },
        { concept: "payment_related_information",  offset: 3,  length: 80 },
        { concept: "addenda_sequence_number",      offset: 83, length: 4 },
        { concept: "entry_detail_sequence_number", offset: 87, length: 7 } ])
end function

function layout_batch_control()
    return finio.layout([
        { concept: "record_type",                     offset: 0,  length: 1 },
        { concept: "service_class_code",              offset: 1,  length: 3 },
        { concept: "entry_addenda_count",             offset: 4,  length: 6 },
        { concept: "entry_hash",                      offset: 10, length: 10 },
        { concept: "total_debit_amount",              offset: 20, length: 12 },
        { concept: "total_credit_amount",             offset: 32, length: 12 },
        { concept: "company_identification",          offset: 44, length: 10 },
        { concept: "message_authentication_code",     offset: 54, length: 19 },
        { concept: "reserved",                        offset: 73, length: 6 },
        { concept: "originating_dfi_identification",  offset: 79, length: 8 },
        { concept: "batch_number",                    offset: 87, length: 7 } ])
end function

function layout_file_control()
    return finio.layout([
        { concept: "record_type",             offset: 0,  length: 1 },
        { concept: "batch_count",             offset: 1,  length: 6 },
        { concept: "block_count",             offset: 7,  length: 6 },
        { concept: "entry_addenda_count",     offset: 13, length: 8 },
        { concept: "entry_hash",              offset: 21, length: 10 },
        { concept: "total_debit_amount",      offset: 31, length: 12 },
        { concept: "total_credit_amount",     offset: 43, length: 12 },
        { concept: "reserved",                offset: 55, length: 39 } ])
end function

function record_kinds()
    return [ "file_header", "batch_header", "entry_detail", "addenda",
             "batch_control", "file_control", "padding" ]
end function

function layout_for(kind)
    if kind = "file_header" then
        return layout_file_header()
    end if
    if kind = "batch_header" then
        return layout_batch_header()
    end if
    if kind = "entry_detail" then
        return layout_entry_detail()
    end if
    if kind = "addenda" then
        return layout_addenda()
    end if
    if kind = "batch_control" then
        return layout_batch_control()
    end if
    if kind = "file_control" then
        return layout_file_control()
    end if
    error "finio_nacha: no layout for record kind '" + string(kind) + "'"
end function

function record_length()
    return 94
end function

' --- transaction codes -----------------------------------------------------
'
' AN EXPLICIT TABLE, NOT A RULE ABOUT THE SECOND DIGIT. The digit rule ("2, 3
' and 4 are credits, 7, 8 and 9 are debits") holds for checking, savings and
' general ledger and BREAKS FOR LOANS, where 55 is a debit; a reader that
' derived the direction would put loan debits on the credit side and the file's
' own control totals would then disagree with it for a reason nothing names. A
' code this table does not hold is reported UNKNOWN and left out of both totals
' (Axiom 7), never defaulted to either side.

function credit_codes()
    return [ "22", "23", "24", "32", "33", "34", "42", "43", "44", "52", "53", "54" ]
end function

function debit_codes()
    return [ "27", "28", "29", "37", "38", "39", "47", "48", "49", "55", "56" ]
end function

function direction_of(code)
    if contains(credit_codes(), code) then
        return "credit"
    end if
    if contains(debit_codes(), code) then
        return "debit"
    end if
    return "unknown"
end function

' --- §7 + Axiom 10: recognition -------------------------------------------
'
' DETERMINISTIC EVIDENCE ONLY. The file header carries a record size of 094, a
' blocking factor of 10 and a format code of 1 at fixed positions; that triple
' is a fingerprint rather than a likelihood, so where it is present with a
' valid record-type sequence the answer is `exact` and where it is absent the
' answer is weaker BY NAME rather than by a number.
'
' FRAMING IS DECIDED HERE because it is evidence about the source and not a
' setting: a file with no line feed whose length divides by 94 is a blocked
' file straight off a mainframe, which is an extremely common way for a real
' ACH file to arrive and which a newline-assuming reader sees as one enormous
' record.

' FRAMING TRAVELS WITH EVERY ANSWER, including the refusals. It is determined
' from the source's shape rather than from whether the source turns out to be
' ACH, and a caller who PINS this adapter on a damaged blocked file still needs
' the records cut at 94 bytes -- defaulting to newlines there would hand them
' one enormous record and a diagnosis about the wrong thing.
function recognise(text)
    reasons = []
    total = byte_count(text)
    if total = 0 then
        return { classification: "unknown", reasons: [ "the source is empty" ] }
    end if
    framing = "lines"
    reclen = 0
    if is_nothing(byte_find(text, chr(10), 0)) then
        ' `floor`, BECAUSE `/` IS FLOAT DIVISION. Written as
        ' `total - (total / 94) * 94` this is ALWAYS zero -- the check never
        ' fired, and a blocked file of the wrong length was accepted and cut
        ' into 94-byte records with a short one at the end. Found by a probe,
        ' not by reading: every fixture happened to be the right length.
        if total != floor(total / 94) * 94 then
            return { classification: "unknown",
                     reasons: [ "no record separator and the length (" + string(total) + ") is not a multiple of 94" ] }
        end if
        framing = "fixed"
        reclen = 94
        append(reasons, "no record separator; " + string(floor(total / 94)) + " blocked 94-byte records")
    end if
    src = finio.open_text(text, { format: "aba.nacha", revision: "unresolved",
                                  framing: framing, record_length: reclen })
    recs = src.records
    if count(recs) = 0 then
        return { classification: "unknown", framing: framing, record_length: reclen,
                 reasons: [ "the source holds no records" ] }
    end if
    n = count(recs)
    ' A trailing empty record is what a final newline produces and is not a
    ' record; counting it as one makes every length check fail on an ordinary
    ' file.
    if byte_count(recs[n - 1]) = 0 then
        n = n - 1
    end if
    if n = 0 then
        return { classification: "unknown", framing: framing, record_length: reclen,
                 reasons: [ "the source holds no records" ] }
    end if
    wrong = 0
    i = 0
    while i < n
        if byte_count(recs[i]) != 94 then
            wrong = wrong + 1
        end if
        i = i + 1
    end while
    if wrong > 0 then
        return { classification: "unknown", framing: framing, record_length: reclen,
                 reasons: [ string(wrong) + " of " + string(n) + " records are not 94 bytes long" ] }
    end if
    append(reasons, string(n) + " records, every one 94 bytes")
    first = recs[0]
    if byte_slice(first, 0, 1) != "1" then
        return { classification: "unknown", framing: framing, record_length: reclen,
                 reasons: [ "the first record is type '" + byte_slice(first, 0, 1) + "', not a file header (1)" ] }
    end if
    seq = _sequence_ok(recs, n)
    if not seq.ok then
        append(reasons, seq.why)
        return { classification: "possible",
                 framing: framing, record_length: reclen, reasons: reasons }
    end if
    append(reasons, "the record-type sequence is well formed")
    size = byte_slice(first, 34, 3)
    blocking = byte_slice(first, 37, 2)
    fmt = byte_slice(first, 39, 1)
    if size = "094" and blocking = "10" and fmt = "1" then
        append(reasons, "the file header carries record size 094, blocking factor 10, format code 1")
        return { classification: "exact", revision: unknown,
                 framing: framing, record_length: reclen, reasons: reasons }
    end if
    append(reasons, ("the file header's record size/blocking factor/format code are '" + size + "'/'" + blocking + "'/'" + fmt + "', not 094/10/1"))
    return { classification: "strong", revision: unknown,
             framing: framing, record_length: reclen, reasons: reasons }
end function

function _raws(records)
    out = []
    for each r in records
        append(out, r.raw)
    end for
    return out
end function

function _sequence_ok(recs, n)
    ' 1 (5 (6 7*)* 8)* 9 9*
    state = "start"
    i = 0
    while i < n
        t = byte_slice(recs[i], 0, 1)
        if state = "start" then
            if t != "1" then
                return { ok: false, why: "record " + string(i) + " is type " + t + " where a file header was due" }
            end if
            state = "in_file"
        else
            if state = "in_file" then
                if t = "5" then
                    state = "in_batch"
                else
                    if t = "9" then
                        state = "after_file"
                    else
                        return { ok: false, why: "record " + string(i) + " is type " + t + " where a batch header or file control was due" }
                    end if
                end if
            else
                if state = "in_batch" then
                    if t = "6" then
                        state = "in_entries"
                    else
                        if t = "8" then
                            state = "in_file"
                        else
                            return { ok: false, why: "record " + string(i) + " is type " + t + " where an entry or batch control was due" }
                        end if
                    end if
                else
                    if state = "in_entries" then
                        if t = "6" or t = "7" then
                            state = "in_entries"
                        else
                            if t = "8" then
                                state = "in_file"
                            else
                                return { ok: false, why: "record " + string(i) + " is type " + t + " where an entry, addenda or batch control was due" }
                            end if
                        end if
                    else
                        if t != "9" then
                            return { ok: false, why: "record " + string(i) + " is type " + t + " after the file control record" }
                        end if
                    end if
                end if
            end if
        end if
        i = i + 1
    end while
    if state != "after_file" then
        return { ok: false, why: "the file ends without a file control record" }
    end if
    return { ok: true, why: "" }
end function

' --- reading ---------------------------------------------------------------
'
' §15: READING PRESERVES AND EXPLAINS, IT DOES NOT JUDGE. A record whose type
' is unrecognised is kept with kind "unknown" and its bytes intact rather than
' dropped, and a batch whose control totals are wrong reads exactly like one
' whose totals are right -- the disagreement is `validate`'s to report. A
' reader that refused would destroy the one thing an operator needs when a file
' is malformed, which is the rest of the file.

function read_source(src, revision)
    recs = src.records
    n = count(recs)
    if n > 0 then
        if byte_count(recs[n - 1]) = 0 then
            n = n - 1
        end if
    end if
    out = []
    loss = []
    i = 0
    while i < n
        raw = recs[i]
        kind = _kind_of(raw)
        entry = { index: i, kind: kind, raw: raw,
                  byte_offset: src.offsets[i], byte_length: byte_count(raw),
                  fields: {} }
        if kind = "padding" or kind = "unknown" then
            if kind = "unknown" then
                append(loss, finio.loss_note("uninterpreted",
                    { record: i, byte_offset: src.offsets[i], byte_length: byte_count(raw),
                      raw: raw,
                      why: "record type '" + byte_slice(raw, 0, 1) + "' is not one this adapter interprets; the bytes are kept" }))
            end if
        else
            lay = layout_for(kind)
            fields = {}
            for each f in lay
                fields[f.concept] = _field(raw, f, kind)
            end for
            entry.fields = fields
        end if
        append(out, entry)
        i = i + 1
    end while
    return { records: out, entities: _entities(out), loss: loss }
end function

function _kind_of(raw)
    if byte_count(raw) < 1 then
        return "unknown"
    end if
    t = byte_slice(raw, 0, 1)
    if t = "1" then
        return "file_header"
    end if
    if t = "5" then
        return "batch_header"
    end if
    if t = "6" then
        return "entry_detail"
    end if
    if t = "7" then
        return "addenda"
    end if
    if t = "8" then
        return "batch_control"
    end if
    if t = "9" then
        ' A file control record and a block-padding record are both type 9 and
        ' are told apart by their CONTENT: padding is 94 nines. Reading the
        ' padding as a file control gives a batch count of 999999 and a hash of
        ' nines -- ordinary-looking numbers, all wrong.
        if raw = repeat("9", 94) then
            return "padding"
        end if
        return "file_control"
    end if
    return "unknown"
end function

' AXIOM 7 LIVES HERE. A blank field is UNKNOWN -- the source said nothing. A
' field that is present and cannot be what it claims is INVALID. And the RAW
' TOKEN IS ALWAYS KEPT beside any mapped meaning, which is §18's third
' requirement: an institution that uses a code slightly differently needs what
' was written, not only this adapter's reading of it.
function _field(raw, spec, kind)
    text = byte_slice(raw, spec.offset, spec.length)
    body = trim(text)
    if byte_count(body) = 0 then
        return { status: "unknown", value: unknown, raw: text }
    end if
    if _is_amount(spec.concept) then
        if not _all_digits(body) then
            return { status: "invalid", value: unknown, raw: text,
                     why: "an amount field must be digits" }
        end if
        return { status: "ok", value: _money(body), raw: text, cents: number(body) }
    end if
    if _is_count(spec.concept) then
        if not _all_digits(body) then
            return { status: "invalid", value: unknown, raw: text,
                     why: "a count field must be digits" }
        end if
        return { status: "ok", value: number(body), raw: text }
    end if
    if spec.concept = "transaction_code" then
        d = direction_of(body)
        if d = "unknown" then
            return { status: "ok", value: body, raw: text, direction: "unknown",
                     why: "this adapter holds no direction for transaction code '" + body + "'" }
        end if
        return { status: "ok", value: body, raw: text, direction: d }
    end if
    return { status: "ok", value: body, raw: text }
end function

function _is_amount(concept)
    return contains([ "amount", "total_debit_amount", "total_credit_amount" ], concept)
end function

function _is_count(concept)
    return contains([ "entry_addenda_count", "entry_hash", "batch_count", "block_count",
                      "batch_number", "addenda_sequence_number",
                      "entry_detail_sequence_number" ], concept)
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

' The decimal point is INSERTED INTO THE DIGITS rather than arrived at by
' dividing, and `money` then parses that text exactly (PLAT-MONEY phase 0).
'
' AND A CLAIM THIS FILE USED TO MAKE AND WITHDREW, because the perturbation
' written to prove it did not go red. The comment here said dividing by 100 in
' floating point was "wrong in the last cent for values a test does not try".
' MEASURED over every shape a NACHA amount field can hold plus 300,000 random
' twelve-digit values: the two agree on ALL of them. A twelve-digit count of
' cents is under 10^12 and doubles carry integers exactly to 2^53, so the
' quotient's shortest round-trip decimal IS the exact one -- there is no value
' this format can represent on which the two part company, and a tier asserting
' they differ would be asserting something false.
'
' What remains true is narrower and is the reason the exact path is still the
' one written: it is exact BY CONSTRUCTION rather than by the field width
' happening to stay inside the double's exact-integer range, so it does not
' have to be re-argued for the next format whose amounts are wider.
function _money(digits)
    s = digits
    while byte_count(s) < 3
        s = "0" + s
    end while
    n = byte_count(s)
    t = byte_slice(s, 0, n - 2) + "." + byte_slice(s, n - 2, 2)
    m {USD}= t
    return m
end function

function _entities(records)
    batches = []
    current = unknown
    header = unknown
    control = unknown
    padding = 0
    for each r in records
        if r.kind = "file_header" then
            header = r.index
        end if
        if r.kind = "padding" then
            padding = padding + 1
        end if
        if r.kind = "file_control" then
            control = r.index
        end if
        if r.kind = "batch_header" then
            current = { header: r.index, entries: [], control: unknown }
        end if
        if r.kind = "entry_detail" then
            if not is_unknown(current) then
                append(current.entries, { entry: r.index, addenda: [] })
            end if
        end if
        if r.kind = "addenda" then
            if not is_unknown(current) then
                k = count(current.entries)
                if k > 0 then
                    append(current.entries[k - 1].addenda, r.index)
                end if
            end if
        end if
        if r.kind = "batch_control" then
            if not is_unknown(current) then
                current.control = r.index
                append(batches, current)
                current = unknown
            end if
        end if
    end for
    if not is_unknown(current) then
        ' A batch with no control record is kept rather than discarded: §15
        ' says a malformed source is preserved as far as safely possible, and
        ' `validate` is what reports the missing record.
        append(batches, current)
    end if
    return { header: header, batches: batches, control: control,
             padding_records: padding }
end function

' --- §15: validation -------------------------------------------------------
'
' THE FILE'S OWN ARITHMETIC IS THE ORACLE. Every batch states its entry count,
' a hash of the routing numbers it touched and its debit and credit totals; the
' file states the same things across batches. Those numbers were computed by
' whoever produced the file, so checking them holds this adapter to arithmetic
' it did not supply -- and a reader that mis-sliced a field, read cents as
' dollars, put loan debits on the credit side or skipped the addenda in a count
' disagrees with them immediately.
'
' TOTALS ARE ACCUMULATED IN CENTS, AS INTEGERS. A twelve-digit amount field
' holds at most 999,999,999,999 cents, and gBASIC's numbers carry integers
' exactly to 2^53, so the arithmetic is exact for any file this format can
' represent; `money` is what the totals are REPORTED as, and dividing by 100 in
' floating point on the way in is the mistake that would make them not be.

function validate_doc(doc)
    issues = []
    recs = doc.records
    n = count(recs)
    ' --- structure ---
    i = 0
    while i < n
        r = recs[i]
        if r.byte_length != 94 then
            append(issues, { code: "record_length", severity: "error", record: i,
                             message: "record " + string(i) + " is " + string(r.byte_length) + " bytes, not 94",
                             expected: "94", found: string(r.byte_length) })
        end if
        if r.kind = "unknown" then
            append(issues, { code: "record_type", severity: "error", record: i,
                             message: "record " + string(i) + " has type '" + byte_slice(r.raw, 0, 1) + "', which is not an ACH record type",
                             found: byte_slice(r.raw, 0, 1) })
        end if
        i = i + 1
    end while
    ' Blocking: the physical file is written in blocks of ten records and is
    ' padded with all-nines records to fill the last one.
    ' THE SAME FLOAT-DIVISION SHAPE AS THE FRAMING CHECK, and it was wrong here
    ' too: `n - (n / 10) * 10` is always zero, so a file whose record count is
    ' not a multiple of the blocking factor was never reported. Found by
    ' sweeping the shape after the framing one, not by reading -- which is the
    ' standing rule that a defect found in one place is evidence about every
    ' place with the same form.
    if n != floor(n / 10) * 10 then
        append(issues, { code: "blocking", severity: "error",
                         message: "the file holds " + string(n) + " records, which is not a multiple of the blocking factor 10",
                         expected: string((floor(n / 10) + 1) * 10), found: string(n) })
    end if
    ' THE SEQUENCE IS CHECKED HERE TOO, and that is not duplication of
    ' `recognise`. Recognition asks "is this file plausibly ACH", and answers
    ' `possible` for one whose records are the right width and whose types run
    ' in an impossible order -- which is the right answer, since it might well
    ' be an ACH file that was damaged. Validation asks the different question
    ' §15 names: does this source conform? Without this, a file recognised as
    ' `possible` reads, validates clean, and reports nothing about the only
    ' thing that was wrong with it.
    seq = _sequence_ok(_raws(recs), n)
    if not seq.ok then
        append(issues, { code: "record_sequence", severity: "error",
                         message: "the record types do not run in a valid order: " + seq.why })
    end if
    ent = doc.entities
    if is_unknown(ent.header) then
        append(issues, { code: "missing_file_header", severity: "error",
                         message: "the file has no file header record (type 1)" })
    end if
    if is_unknown(ent.control) then
        append(issues, { code: "missing_file_control", severity: "error",
                         message: "the file has no file control record (type 9)" })
    end if
    ' --- the file header's own declarations ---
    if not is_unknown(ent.header) then
        h = recs[ent.header].fields
        for each pair in [ [ "record_size", "094" ], [ "blocking_factor", "10" ], [ "format_code", "1" ] ]
            got = h[pair[0]]
            if got.status != "ok" or got.value != pair[1] then
                append(issues, { code: "header_declaration", severity: "warning",
                                 record: ent.header, concept: pair[0],
                                 message: "the file header declares " + pair[0] + " '" + string(got.value) + "', not '" + pair[1] + "'",
                                 expected: pair[1], found: string(got.value) })
            end if
        end for
    end if
    ' --- each batch against its own control record ---
    file_entries = 0
    file_hash = 0
    file_debit = 0
    file_credit = 0
    for each b in ent.batches
        if is_unknown(b.control) then
            append(issues, { code: "missing_batch_control", severity: "error",
                             record: b.header,
                             message: "the batch beginning at record " + string(b.header) + " has no batch control record (type 8)" })
        else
            t = batch_totals(recs, b)
            c = recs[b.control].fields
            hdr = recs[b.header].fields
            file_entries = file_entries + t.count
            file_hash = file_hash + t.hash
            file_debit = file_debit + t.debit
            file_credit = file_credit + t.credit
            append(issues, _compare_count(c, "entry_addenda_count", t.count, b.control, "batch_entry_count",
                "the batch control counts"))
            append(issues, _compare_count(c, "entry_hash", _hash10(t.hash), b.control, "batch_entry_hash",
                "the batch control's entry hash is"))
            append(issues, _compare_amount(c, "total_debit_amount", t.debit, b.control, "batch_debit_total",
                "the batch control's debit total is"))
            append(issues, _compare_amount(c, "total_credit_amount", t.credit, b.control, "batch_credit_total",
                "the batch control's credit total is"))
            ' The service class code appears on BOTH the batch header and its
            ' control, and a disagreement between them is a defect in the file
            ' that neither record can reveal on its own.
            if hdr.service_class_code.value != c.service_class_code.value then
                append(issues, { code: "service_class", severity: "error", record: b.control,
                                 concept: "service_class_code",
                                 message: "the batch header says service class '" + string(hdr.service_class_code.value) + "' and its control record says '" + string(c.service_class_code.value) + "'",
                                 expected: string(hdr.service_class_code.value),
                                 found: string(c.service_class_code.value) })
            end if
        end if
        for each e in b.entries
            code = recs[e.entry].fields.transaction_code
            if has(code, "direction") then
                if code.direction = "unknown" then
                    append(issues, { code: "unknown_transaction_code", severity: "warning",
                                     record: e.entry, concept: "transaction_code",
                                     message: "transaction code '" + string(code.value) + "' has no direction in this adapter, so its amount is in neither total",
                                     found: string(code.value) })
                end if
            end if
        end for
    end for
    ' --- the file control against the whole file ---
    if not is_unknown(ent.control) then
        fc = recs[ent.control].fields
        append(issues, _compare_count(fc, "batch_count", count(ent.batches), ent.control, "file_batch_count",
            "the file control counts"))
        append(issues, _compare_count(fc, "entry_addenda_count", file_entries, ent.control, "file_entry_count",
            "the file control counts"))
        append(issues, _compare_count(fc, "entry_hash", _hash10(file_hash), ent.control, "file_entry_hash",
            "the file control's entry hash is"))
        append(issues, _compare_amount(fc, "total_debit_amount", file_debit, ent.control, "file_debit_total",
            "the file control's debit total is"))
        append(issues, _compare_amount(fc, "total_credit_amount", file_credit, ent.control, "file_credit_total",
            "the file control's credit total is"))
        append(issues, _compare_count(fc, "block_count", floor(n / 10), ent.control, "block_count",
            "the file control counts"))
    end if
    return _flatten(issues)
end function

' `append` on an array whose elements are arrays would nest them, and a caller
' reading `issues[3].code` on a nested one gets `unknown` rather than an error
' -- so the comparison helpers return an ARRAY (empty when they agree) and this
' flattens once, deliberately, rather than every call site testing for nothing.
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

function batch_totals(recs, b)
    cnt = 0
    hash = 0
    debit = 0
    credit = 0
    for each e in b.entries
        f = recs[e.entry].fields
        cnt = cnt + 1 + count(e.addenda)
        rdfi = f.receiving_dfi_identification
        if rdfi.status = "ok" then
            if _all_digits(rdfi.value) then
                hash = hash + number(rdfi.value)
            end if
        end if
        amt = f.amount
        dir = "unknown"
        if has(f.transaction_code, "direction") then
            dir = f.transaction_code.direction
        end if
        if amt.status = "ok" then
            if dir = "debit" then
                debit = debit + amt.cents
            end if
            if dir = "credit" then
                credit = credit + amt.cents
            end if
        end if
    end for
    return { count: cnt, hash: hash, debit: debit, credit: credit }
end function

' The entry hash is the SUM of the eight-digit routing numbers with only the
' RIGHTMOST TEN DIGITS kept. Keeping the whole sum is the plausible wrong
' answer: it agrees with the file for every small batch and parts company
' exactly when a file is large, which is when nobody is checking by hand.
function _hash10(total)
    return total - floor(total / 10000000000) * 10000000000
end function

function _compare_count(fields, concept, want, record_index, code, phrase)
    got = fields[concept]
    if got.status != "ok" then
        return [ { code: code, severity: "error", record: record_index, concept: concept,
                   message: phrase + " unreadable ('" + string(got.raw) + "'); the entries give " + string(want),
                   expected: string(want), found: string(got.raw) } ]
    end if
    if got.value = want then
        return []
    end if
    return [ { code: code, severity: "error", record: record_index, concept: concept,
               message: phrase + " " + string(got.value) + "; the entries give " + string(want),
               expected: string(want), found: string(got.value) } ]
end function

function _compare_amount(fields, concept, want_cents, record_index, code, phrase)
    got = fields[concept]
    if got.status != "ok" then
        return [ { code: code, severity: "error", record: record_index, concept: concept,
                   message: phrase + " unreadable ('" + string(got.raw) + "'); the entries give " + string(_money(_digits(want_cents, 12))),
                   expected: string(want_cents), found: string(got.raw) } ]
    end if
    if got.cents = want_cents then
        return []
    end if
    return [ { code: code, severity: "error", record: record_index, concept: concept,
               message: phrase + " " + string(got.value) + "; the entries give " + string(_money(_digits(want_cents, 12))),
               expected: string(want_cents), found: string(got.cents) } ]
end function

function _digits(n, width)
    s = string(floor(n))
    while byte_count(s) < width
        s = "0" + s
    end while
    if byte_count(s) > width then
        error ("finio_nacha: " + s + " does not fit a " + string(width) + "-digit field")
    end if
    return s
end function

' --- §16 + §17: writing ----------------------------------------------------
'
' BYTE FIDELITY IS REAL HERE AND IT IS RECONSTRUCTED, NOT RETURNED. `write_doc`
' emits each record's retained bytes joined by the separator that record
' actually carried, so a document read and written unchanged reproduces its
' source exactly -- including CRLF where the source had CRLF, and no separator
' at all where the source was blocked. Returning `doc.source.text` would pass
' the same test and prove nothing, which is why the suite also asserts that a
' CHANGED document produces DIFFERENT bytes.

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

' Rewriting one field in place. The width is the format's, so a value that does
' not fit is REFUSED rather than truncated: a truncated account number or a
' truncated amount is a perfectly well-formed record that means something else.
function set_field(doc, record_index, concept, text)
    if record_index < 0 or record_index >= count(doc.records) then
        error "finio_nacha.set_field: no record " + string(record_index) + " in this document"
    end if
    r = doc.records[record_index]
    if r.kind = "padding" or r.kind = "unknown" then
        error ("finio_nacha.set_field: record " + string(record_index) + " is " + r.kind + " and has no fields")
    end if
    lay = layout_for(r.kind)
    spec = unknown
    for each f in lay
        if f.concept = concept then
            spec = f
        end if
    end for
    if is_unknown(spec) then
        error ("finio_nacha.set_field: a " + r.kind + " record has no concept '" + string(concept) + "'")
    end if
    if byte_count(text) != spec.length then
        error ("finio_nacha.set_field: '" + concept + "' is " + string(spec.length) + " bytes and the value given is " + string(byte_count(text)) + " -- pad or refuse, never truncate")
    end if
    raw = byte_slice(r.raw, 0, spec.offset) + text + byte_slice(r.raw, spec.offset + spec.length, byte_count(r.raw) - spec.offset - spec.length)
    updated = doc
    updated.records[record_index].raw = raw
    updated.records[record_index].fields[concept] = _field(raw, spec, r.kind)
    return updated
end function

' --- the adapter -----------------------------------------------------------

function registry_entry()
    return { id: "aba.nacha",
             name: "ACH file (NACHA record format)",
             family: "payments",
             domain: "US ACH origination and return files",
             authority: "Nacha (National Automated Clearing House Association)",
             description: "94-character fixed-width records: file header, batches of entry details with optional addenda, batch and file control records carrying counts, a routing-number hash and debit and credit totals.",
             representation: "fixed-width, record-oriented, ASCII",
             transport: "file, usually delivered to or from an originating depository financial institution",
             known_revisions: [ "unresolved" ],
             specification_sources: [
               { source_type: "implementation_guide",
                 source_url_or_reference: "NACHA File Format - Formatting Guide, Regions Bank, dated 6/20/2019: https://www.regions.com/-/media/pdfs/treasury-management/NACHA_File_Layout_Guide.pdf",
                 date_retrieved: "2026-09-15",
                 note: "field-level positions for all six record types, 1-based inclusive. EVERY LAYOUT IN THIS ADAPTER WAS CHECKED AGAINST IT -- 61 fields, tests/finio/nacha_positions_test.bas -- which is the first statement of those positions that did not come from this project." } ],
             acquisition_class: "DE_FACTO",
             ' DE_FACTO AND NOT OPEN, conservatively. Nacha itself publishes an
             ' ACH Guide for Developers, which would make the file format OPEN
             ' by §10's definition -- but it answered HTTP 403 to an automated
             ' fetch on 2026-09-15 and nothing here rests on it, and claiming
             ' OPEN on a document that was not opened is the kind of
             ' unevidenced claim this registry exists to prevent. What WAS
             ' retrieved is several independent bank guides that agree, which
             ' is exactly §10's DE_FACTO: "sufficient lawful public evidence
             ' exists to characterize the format". Upgrading this entry is a
             ' queue item, not a judgement call.
             spec_public: true,
             spec_acquisition_method: "The RECORD LAYOUTS are freely published, both in bank ACH origination guides (retrieved, above) and in Nacha's own ACH Guide for Developers at achdevguide.nacha.org (found 2026-09-15, HTTP 403 to an automated fetch, not used). The NACHA OPERATING RULES are a different artefact: they govern participation in the ACH network rather than the file layout, they are a paid publication, and they are NOT held.",
             implementation_allowed: true,
             spec_redistribution_allowed: false,
             sample_redistribution_allowed: false,
             ' `researched` IS NOW EVIDENCED, where before it was asserted. The
             ' claim is not that a specification was obtained -- it is that
             ' enough is understood to implement, and a retrieved field-level
             ' guide that all six layouts agree with is what makes that
             ' checkable rather than a statement about my own confidence.
             state: "researched",
             recognition_status: "implemented",
             read_status: "implemented",
             ' RE-EMISSION, NOT ORIGINATION, and the distinction matters to
             ' anyone reading this entry to decide whether they can use it.
             ' This adapter reproduces a file it read -- byte for byte if
             ' nothing changed, and with one field rewritten in place if
             ' something did. It does NOT build a file from nothing: an
             ' originator has to decide a company identification, an ODFI, an
             ' effective entry date and a service class, and those are the
             ' consumer's judgements (Axiom 9), not the format's. Saying
             ' "implemented" here would read as "you can originate with this".
             write_status: "re-emission only; this adapter does not originate a file",
             validation_status: "implemented",
             test_vectors: [ "tests/finio/nacha/*.ach -- written here, not produced by a bank",
                             "tests/finio/nacha_positions.txt -- field positions transcribed from the guide above, the one check on the layouts that did not originate here" ],
             known_variants: [],
             known_extensions: [],
             maintenance_priority: "active",
             watch_sources: [
               { kind: "document",
                 reference: "https://achdevguide.nacha.org/ach-file-overview",
                 watching: "Nacha's own developer guide becoming retrievable, which would move this entry from DE_FACTO to OPEN" },
               { kind: "document",
                 reference: "https://www.regions.com/-/media/pdfs/treasury-management/NACHA_File_Layout_Guide.pdf",
                 watching: "a change to the published field positions, which would mean the record layout itself moved" } ],
             last_reviewed: "2026-09-14",
             next_review_due: "2027-09-14" }
end function

function adapter()
    return finio.adapter({
        id: "aba.nacha",
        ' ONE REVISION, AND ITS NAME IS THE HONEST ANSWER. The Nacha Operating
        ' Rules are revised every year; the RECORD LAYOUT has outlived twenty
        ' of those revisions and a file does not say which year's rules it was
        ' produced under. Naming a year here would put that year in every
        ' document this adapter ever wrote, on no evidence at all.
        revisions: [ "unresolved" ],
        recognise: recognise,
        read: read_source,
        validate: validate_doc,
        write: write_doc,
        registry_entry: registry_entry(),
        ' Every record is retained verbatim and re-emitted with the separator
        ' it arrived with, so an unchanged document reproduces its source bytes.
        byte_fidelity: true,
        semantic_fidelity: true })
end function
end library
