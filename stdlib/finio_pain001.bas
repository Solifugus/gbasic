' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' finio_pain001 -- ISO 20022 pain.001, a customer credit transfer initiation,
' and THE FIRST ADAPTER IN THIS TREE FOR A FILE YOU SEND.
' See docs/financial_adapters_design.md §16, §20.
'
' EVERY ADAPTER BEFORE THIS ONE READS A REPORT. An ACH file, a bank statement,
' a balance report, an OFX download -- all of them arrive, and all four say
' some version of "re-emission only" for `write_status`, because originating
' one means deciding things Axiom 9 says belong to the consumer.
'
' A pain.001 IS AN INSTRUCTION. It is the message a business sends its bank to
' say "make these payments", and that inverts where the risk lives. Reading a
' statement wrongly gives you a wrong number on a screen. Writing a pain.001
' wrongly gives you a payment run the bank REJECTS -- or worse, executes.
'
' WHICH IS WHY §16 EXISTS AND WHY IT WAS UNIMPLEMENTED UNTIL NOW. "Writing
' requires stronger guarantees than reading... Before serialization, the
' framework should be able to classify the requested conversion as
' representable, lossy or impossible... The default should favor refusal when
' semantic information would be silently lost." Four read-only adapters never
' put any weight on that sentence.
'
' THE CONTROL TOTALS ARE THE POINT, AND THEY POINT THE OTHER WAY. A pain.001
' states NbOfTxs and CtrlSum at TWO levels -- once per payment-information
' block and once for the message. Reading a file, those are somebody else's
' arithmetic to check. WRITING one, they are arithmetic YOU MUST GET RIGHT, and
' a bank that finds them disagreeing rejects the file. So the same numbers that
' were a test oracle in every previous adapter are, here, the thing the adapter
' is FOR.
'
' WHAT IT KNOWS AND HOW (Axiom 11). ISO 20022 publishes its message definitions
' and schemas without charge, which is why this entry says OPEN. The element
' names were read from SEPA credit transfer examples published under MIT by the
' sepa-pain001-examples project. No schema was downloaded and no bank has ever
' accepted a file this adapter wrote, which is what `verified` would mean and
' why the registry entry does not claim it.

library finio_pain001

load finio from "finio.bas"
load finio_iso20022 from "finio_iso20022.bas" as iso
load xml

function revisions()
    return [ "pain.001.001.03" ]
end function

' --- §7 + Axiom 10: recognition -------------------------------------------

function recognise(text)
    reasons = []
    if byte_count(text) = 0 then
        return { classification: "unknown", reasons: [ "the source is empty" ] }
    end if
    msg = iso.declared_message(text)
    if byte_count(msg) = 0 then
        return { classification: "unknown",
                 reasons: [ "no ISO 20022 namespace in this source" ] }
    end if
    if not iso.is_family(msg, "pain.001") then
        return { classification: "unknown",
                 reasons: [ "the document declares " + msg + ", which is not pain.001" ] }
    end if
    append(reasons, "the document namespace declares " + msg)
    if is_nothing(byte_find(text, "<Document", 0)) then
        append(reasons, "but there is no <Document> root element")
        return { classification: "possible", revision: msg, reasons: reasons }
    end if
    if is_nothing(byte_find(text, "<CstmrCdtTrfInitn", 0)) then
        append(reasons, "but no <CstmrCdtTrfInitn>, which pain.001 requires")
        return { classification: "strong", revision: msg, reasons: reasons }
    end if
    append(reasons, "carrying <CstmrCdtTrfInitn>")
    return { classification: "exact", revision: msg, reasons: reasons }
end function

' --- reading ---------------------------------------------------------------

function read_source(src, revision)
    doc = xml.parse(src.text)
    records = []
    loss = []
    base = "CstmrCdtTrfInitn"
    header = unknown
    gh = xml.find(doc, base + "/GrpHdr")
    if not is_unknown(gh) then
        p = base + "/GrpHdr"
        header = count(records)
        append(records, { index: header, kind: "group_header", path: p, occurrence: 0,
                          fields: { message_id: iso.text_field(gh, "MsgId", p + "/MsgId", 0),
                                    created: iso.text_field(gh, "CreDtTm", p + "/CreDtTm", 0),
                                    number_of_transactions: iso.number_field(gh, "NbOfTxs", p + "/NbOfTxs", 0),
                                    control_sum: iso.text_field(gh, "CtrlSum", p + "/CtrlSum", 0),
                                    initiating_party: iso.text_field(gh, "InitgPty/Nm", p + "/InitgPty/Nm", 0) } })
    end if
    blocks = []
    bi = 0
    for each pi in xml.find_all(doc, base + "/PmtInf")
        p = base + "/PmtInf"
        idx = count(records)
        append(records, { index: idx, kind: "payment_info", path: p, occurrence: bi,
                          fields: { payment_info_id: iso.text_field(pi, "PmtInfId", p + "/PmtInfId", bi),
                                    payment_method: iso.text_field(pi, "PmtMtd", p + "/PmtMtd", bi),
                                    batch_booking: iso.text_field(pi, "BtchBookg", p + "/BtchBookg", bi),
                                    number_of_transactions: iso.number_field(pi, "NbOfTxs", p + "/NbOfTxs", bi),
                                    control_sum: iso.text_field(pi, "CtrlSum", p + "/CtrlSum", bi),
                                    service_level: iso.text_field(pi, "PmtTpInf/SvcLvl/Cd", p + "/PmtTpInf/SvcLvl/Cd", bi),
                                    requested_execution_date: iso.text_field(pi, "ReqdExctnDt", p + "/ReqdExctnDt", bi),
                                    debtor: iso.text_field(pi, "Dbtr/Nm", p + "/Dbtr/Nm", bi),
                                    debtor_iban: iso.text_field(pi, "DbtrAcct/Id/IBAN", p + "/DbtrAcct/Id/IBAN", bi),
                                    debtor_bic: iso.text_field(pi, "DbtrAgt/FinInstnId/BIC", p + "/DbtrAgt/FinInstnId/BIC", bi),
                                    charge_bearer: iso.text_field(pi, "ChrgBr", p + "/ChrgBr", bi) } })
        txs = []
        ti = 0
        for each tx in xml.find_all(pi, "CdtTrfTxInf")
            tp = p + "/CdtTrfTxInf"
            tidx = count(records)
            append(records, { index: tidx, kind: "transaction", path: tp, occurrence: ti,
                              fields: { instruction_id: iso.text_field(tx, "PmtId/InstrId", tp + "/PmtId/InstrId", ti),
                                        end_to_end_id: iso.text_field(tx, "PmtId/EndToEndId", tp + "/PmtId/EndToEndId", ti),
                                        amount: iso.amount_field(tx, "Amt/InstdAmt", tp + "/Amt/InstdAmt", ti),
                                        creditor: iso.text_field(tx, "Cdtr/Nm", tp + "/Cdtr/Nm", ti),
                                        creditor_iban: iso.text_field(tx, "CdtrAcct/Id/IBAN", tp + "/CdtrAcct/Id/IBAN", ti),
                                        creditor_bic: iso.text_field(tx, "CdtrAgt/FinInstnId/BIC", tp + "/CdtrAgt/FinInstnId/BIC", ti),
                                        remittance: iso.text_field(tx, "RmtInf/Ustrd", tp + "/RmtInf/Ustrd", ti) } })
            append(txs, tidx)
            ti = ti + 1
        end for
        append(blocks, { payment_info: idx, transactions: txs })
        bi = bi + 1
    end for
    return { records: records,
             entities: { header: header, blocks: blocks },
             loss: loss }
end function

' --- §15: validation -------------------------------------------------------
'
' TWO LEVELS OF CONTROL TOTAL, and for this message they are what a bank checks
' before it does anything else. A disagreement is not a report that reads oddly
' -- it is a file that comes back rejected.
'
' THE CURRENCY CHECK IS THE ONE THAT IS NOT OBVIOUS. A CtrlSum carries NO
' currency of its own, so it is only meaningful if every amount it totals is in
' the same one. A payment-information block mixing EUR and USD has a control
' sum that means nothing, and `money` will not add them anyway -- so the
' mismatch is REPORTED and the block is excluded from the total rather than
' allowed to raise (§15: a malformed source is described, not died on).

function validate_doc(doc)
    issues = []
    recs = doc.records
    ent = doc.entities
    if is_unknown(ent.header) then
        append(issues, { code: "missing_group_header", severity: "error",
                         message: "the message has no GrpHdr" })
    end if
    if count(ent.blocks) = 0 then
        append(issues, { code: "no_payment_information", severity: "error",
                         message: "the message carries no PmtInf block, so it instructs nothing" })
    end if
    total_txs = 0
    total = unknown
    total_ccy = ""
    usable = true
    for each b in ent.blocks
        pf = recs[b.payment_info].fields
        t = block_total(recs, b)
        total_txs = total_txs + count(b.transactions)
        for each bad in t.problems
            append(issues, bad)
        end for
        if not t.usable then
            usable = false
        end if
        append(issues, _compare_count(pf, "number_of_transactions", count(b.transactions),
            b.payment_info, "block_transaction_count", "this payment block counts"))
        if t.usable then
            append(issues, _compare_sum(pf, "control_sum", t.total, t.currency,
                b.payment_info, "block_control_sum", "this payment block's control sum is"))
            if is_unknown(total) then
                total = t.total
                total_ccy = t.currency
            else
                if total_ccy = t.currency then
                    total = total + t.total
                else
                    usable = false
                    append(issues, { code: "mixed_currency_message", severity: "error",
                                     record: b.payment_info,
                                     message: ("this payment block is in " + t.currency
                                               + " and an earlier one is in " + total_ccy
                                               + ", so the message control sum totals two currencies") })
                end if
            end if
        end if
        ' EVERY TRANSACTION NEEDS AN END-TO-END REFERENCE. It is the value that
        ' comes back on the statement, and it is how a business reconciles what
        ' it sent against what cleared. A missing one is accepted by the schema
        ' and leaves a payment that cannot be matched to anything afterwards.
        for each ti in b.transactions
            f = recs[ti].fields
            if f.end_to_end_id.status != "ok" then
                append(issues, { code: "missing_end_to_end_id", severity: "error", record: ti,
                                 message: ("the transaction at " + finio.describe_location(f.amount.location)
                                           + " carries no EndToEndId, so what clears cannot be matched to what was sent") })
            end if
            if f.creditor_iban.status != "ok" then
                append(issues, { code: "missing_creditor_account", severity: "error", record: ti,
                                 message: "a transaction names no creditor account" })
            end if
        end for
    end for
    if not is_unknown(ent.header) then
        hf = recs[ent.header].fields
        append(issues, _compare_count(hf, "number_of_transactions", total_txs,
            ent.header, "message_transaction_count", "the group header counts"))
        if usable and not is_unknown(total) then
            append(issues, _compare_sum(hf, "control_sum", total, total_ccy,
                ent.header, "message_control_sum", "the group header's control sum is"))
        end if
    end if
    return _flatten(issues)
end function

function block_total(recs, b)
    total = unknown
    ccy = ""
    problems = []
    usable = true
    for each ti in b.transactions
        a = recs[ti].fields.amount
        if a.status != "ok" then
            usable = false
            append(problems, { code: "unreadable_amount", severity: "error", record: ti,
                               message: ("the amount at " + finio.describe_location(a.location)
                                         + " cannot be read, so this block's control sum cannot be checked") })
        else
            if byte_count(ccy) = 0 then
                ccy = a.currency
                total = a.value
            else
                if ccy != a.currency then
                    usable = false
                    append(problems, { code: "mixed_currency_block", severity: "error", record: ti,
                                       message: ("this payment block mixes " + ccy + " and " + string(a.currency)
                                                 + ", and a control sum carries no currency of its own -- so it can total only one") })
                else
                    total = total + a.value
                end if
            end if
        end if
    end for
    return { total: total, currency: ccy, usable: usable, problems: problems }
end function

function _compare_count(fields, concept, want, record_index, code, phrase)
    got = fields[concept]
    if got.status != "ok" then
        return [ { code: code, severity: "error", record: record_index, concept: concept,
                   message: phrase + " nothing readable; there are " + string(want),
                   expected: string(want), found: string(got.raw) } ]
    end if
    if got.value = want then
        return []
    end if
    return [ { code: code, severity: "error", record: record_index, concept: concept,
               message: (phrase + " " + string(got.value) + "; there are " + string(want)
                         + " (" + finio.describe_location(got.location) + ")"),
               expected: string(want), found: string(got.value) } ]
end function

function _compare_sum(fields, concept, want, ccy, record_index, code, phrase)
    got = fields[concept]
    if got.status != "ok" then
        return [ { code: code, severity: "error", record: record_index, concept: concept,
                   message: phrase + " missing; the transactions give " + money.text(want, 2),
                   expected: money.text(want, 2), found: "" } ]
    end if
    stated = iso.money_of(ccy, got.value)
    if is_unknown(stated) then
        return [ { code: code, severity: "error", record: record_index, concept: concept,
                   message: phrase + " '" + string(got.value) + "', which is not a decimal amount",
                   found: string(got.value) } ]
    end if
    if stated = want then
        return []
    end if
    return [ { code: code, severity: "error", record: record_index, concept: concept,
               message: (phrase + " " + money.text(stated, 2) + "; the transactions give "
                         + money.text(want, 2) + " (" + finio.describe_location(got.location) + ")"),
               expected: money.text(want, 2), found: money.text(stated, 2) } ]
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

' --- §16: WRITING, AND WHAT IT REFUSES ------------------------------------
'
' "Writing requires stronger guarantees than reading... Before serialization,
' the framework should be able to classify the requested conversion as
' representable, lossy or impossible... The default should favor refusal when
' semantic information would be silently lost." (§16)
'
' THE LIMITS BELOW ARE THE SCHEME'S, NOT THE SCHEMA'S, and that distinction is
' the whole of why this matters. The XSD will happily accept a 200-character
' creditor name; the SEPA credit transfer scheme allows 70, and a bank
' truncates or rejects. An adapter that serialized it would produce a file that
' VALIDATES and gets sent and comes back refused -- or worse, pays the right
' amount to a name nobody can match.
'
' SO A NAME TOO LONG IS `lossy` AND IS REFUSED BY DEFAULT, not truncated. A
' currency the scheme does not carry is `impossible` and is refused outright,
' which is a different answer and deserves to be: one a caller can override
' having understood the cost, the other never.

function scheme_limits()
    return { creditor_name: 70, debtor_name: 70, remittance: 140,
             end_to_end_id: 35, instruction_id: 35, payment_info_id: 35,
             message_id: 35 }
end function

function classify_write(doc)
    losses = []
    impossible = []
    recs = doc.records
    lim = scheme_limits()
    ent = doc.entities
    ' THE CHECK RETURNS ITS FINDING; IT DOES NOT APPEND TO A PARAMETER. Written
    ' the other way -- passing `losses` in and appending inside -- every loss
    ' was discarded, because `append` inside a function mutates a LOCAL COPY of
    ' an array that arrived as an argument. The classification came back
    ' `representable` for a document with a 73-character creditor name, which
    ' is the permissive direction: a file that would be truncated or rejected,
    ' declared fit to send.
    if not is_unknown(ent.header) then
        losses = _add(losses, _too_long(recs[ent.header].fields, "message_id", lim.message_id, ent.header))
    end if
    for each b in ent.blocks
        pf = recs[b.payment_info].fields
        losses = _add(losses, _too_long(pf, "payment_info_id", lim.payment_info_id, b.payment_info))
        losses = _add(losses, _too_long(pf, "debtor", lim.debtor_name, b.payment_info))
        for each ti in b.transactions
            f = recs[ti].fields
            losses = _add(losses, _too_long(f, "creditor", lim.creditor_name, ti))
            losses = _add(losses, _too_long(f, "remittance", lim.remittance, ti))
            losses = _add(losses, _too_long(f, "end_to_end_id", lim.end_to_end_id, ti))
            losses = _add(losses, _too_long(f, "instruction_id", lim.instruction_id, ti))
            ' THE SCHEME IS EURO. A payment in another currency is not something
            ' this message can carry under it -- not a truncation a caller might
            ' accept, but a fact about the scheme, so it is `impossible` and has
            ' no override.
            a = f.amount
            if a.status = "ok" and not is_unknown(a.currency) and a.currency != "EUR" then
                impossible = _add(impossible, { record: ti, concept: "amount",
                                     why: ("the SEPA credit transfer scheme carries EUR and this amount is in "
                                           + string(a.currency) + "; no truncation makes it representable") })
            end if
        end for
    end for
    classification = "representable"
    if count(losses) > 0 then
        classification = "lossy"
    end if
    if count(impossible) > 0 then
        classification = "impossible"
    end if
    return { classification: classification, losses: losses, impossible: impossible,
             ' §16: "If information cannot be represented in the target format,
             ' the framework must report exactly what cannot be represented."
             why: _describe(classification, losses, impossible) }
end function

function _add(items, found)
    if is_unknown(found) then
        return items
    end if
    out = items
    append(out, found)
    return out
end function

' LENGTH IN CHARACTERS, not bytes, because that is what the scheme counts -- a
' 70-character limit on a name is 70 characters whatever they encode to.
function _too_long(fields, concept, limit, record_index)
    if not has(fields, concept) then
        return unknown
    end if
    f = fields[concept]
    if f.status != "ok" then
        return unknown
    end if
    n = len(string(f.value))
    if n <= limit then
        return unknown
    end if
    return { record: record_index, concept: concept, length: n, limit: limit,
             why: (concept + " is " + string(n) + " characters and the scheme carries "
                   + string(limit) + "; writing it would truncate " + string(n - limit)) }
end function

function _describe(classification, losses, impossible)
    if classification = "representable" then
        return "every value fits what the scheme carries"
    end if
    parts = []
    for each i in impossible
        append(parts, string(i.why))
    end for
    for each l in losses
        append(parts, string(l.why))
    end for
    return join(parts, "; ")
end function

' THE SOURCE IS RE-EMITTED. This adapter does not yet BUILD a pain.001 from
' payments -- what it does is read one, let a caller inspect and check it, and
' write it back. `classify_write` is the half that had to exist first: an
' originator's real question is not "can you serialize this" but "will the bank
' take it", and the answer to that is the classification above plus the control
' totals `validate` checks.
function write_doc(doc)
    c = classify_write(doc)
    ' REFUSED HERE AS WELL AS AT THE FRAMEWORK BOUNDARY, deliberately. §16's
    ' rule lives in `finio.write_text`, and this second guard means a caller
    ' reaching the adapter's writer directly cannot get past it -- the same
    ' treatment R9 gets in `reasoning`, where a refusal is enforced at the
    ' boundary AND structurally so a hand-built value cannot smuggle one
    ' through. Measured: removing the framework check alone still refused,
    ' which is what a defence in depth is supposed to do and is why it is
    ' written down rather than left to look like duplication.
    if c.classification = "impossible" then
        error ("finio_pain001: this document cannot be written under the SEPA credit transfer scheme -- " + c.why)
    end if
    notes = []
    for each l in c.losses
        append(notes, finio.loss_note("narrowed",
            { record: l.record, concept: l.concept, why: l.why }))
    end for
    return { text: doc.source.text, loss: notes }
end function

' --- the adapter -----------------------------------------------------------

function registry_entry()
    return { id: "iso20022.pain001",
             name: "ISO 20022 pain.001 customer credit transfer initiation",
             family: "payments",
             domain: "corporate-to-bank payment instruction",
             authority: "ISO 20022 Registration Management Group",
             description: "XML payment initiation: a group header stating a transaction count and control sum, then payment information blocks each carrying the debtor, the account to draw on, an execution date and its own count and control sum, and one credit transfer transaction per beneficiary.",
             representation: "hierarchical XML, namespaced by message version",
             transport: "file or message, delivered to the executing bank",
             known_revisions: [ "pain.001.001.02", "pain.001.001.03", "pain.001.001.04",
                                "pain.001.001.05", "pain.001.001.06", "pain.001.001.07",
                                "pain.001.001.08", "pain.001.001.09", "pain.001.001.10",
                                "pain.001.001.11", "pain.001.001.12" ],
             specification_sources: [
               { source_type: "standards_body",
                 source_url_or_reference: "ISO 20022 message definitions catalogue: https://www.iso20022.org/iso-20022-message-definitions",
                 date_retrieved: "2026-09-15",
                 note: "message definitions and XSDs published without charge. The CATALOGUE was retrieved; no schema was downloaded and nothing here has been validated against one." },
               { source_type: "public_sample",
                 source_url_or_reference: "https://github.com/SynapticaSolution/sepa-pain001-examples -- SEPA credit transfer examples",
                 date_retrieved: "2026-09-15",
                 note: "MIT-licensed, CBI-compliant SEPA samples, read BEFORE this adapter was written. They carry pain.001.001.03 and a pain.008 direct debit, which is what established that this family's revision lives in the namespace exactly as camt's does." } ],
             acquisition_class: "OPEN",
             spec_public: true,
             spec_acquisition_method: "Free download from the ISO 20022 catalogue; no registration, purchase or membership.",
             implementation_allowed: true,
             spec_redistribution_allowed: false,
             sample_redistribution_allowed: false,
             state: "researched",
             recognition_status: "implemented",
             read_status: "implemented",
             ' THE FIRST ADAPTER HERE THAT CLASSIFIES A WRITE (§16). It still
             ' re-emits rather than originating -- building a pain.001 from
             ' payments means deciding a debtor account, an execution date and a
             ' charge bearer, which are the consumer's (Axiom 9) -- but the
             ' guarantee §16 asks for is implemented: before serializing, the
             ' conversion is classified representable, lossy or impossible, a
             ' lossy one is REFUSED by default rather than truncated, and an
             ' impossible one has no override.
             write_status: "re-emission with a §16 write classification; this adapter does not originate a payment run",
             validation_status: "implemented",
             test_vectors: [
               { name: "tests/finio/pain/*.xml",
                 source_url_or_reference: "generated by tools/make_pain001_fixture.py in this repository",
                 date_retrieved: "2026-09-15",
                 licence: "Apache-2.0 (this project)",
                 usage: "permitted", redistribution: "permitted",
                 note: "written here. Supplies what the foreign corpus cannot: a message with SEVERAL payment blocks, so the group control sum is a real total rather than a copy of the one block's; and the scheme violations no publisher ships on purpose." },
               { name: "tests/finio/foreign_pain/*.xml",
                 source_url_or_reference: "https://github.com/SynapticaSolution/sepa-pain001-examples",
                 date_retrieved: "2026-09-15",
                 licence: "MIT",
                 usage: "permitted", redistribution: "permitted",
                 note: "redistributed with the licence alongside. Read BEFORE the adapter was written." } ],
             known_variants: [
               "every bank publishes its own implementation guideline narrowing which optional elements it accepts, so a document valid against the schema is routinely rejected by a bank -- which is why §16's classification here checks the SCHEME's limits rather than the schema's" ],
             known_extensions: [
               "pain.008 direct debit is a sibling message in the same family and is NOT implemented; it is recognised as not-pain.001 and refused rather than read" ],
             maintenance_priority: "active",
             watch_sources: [
               { kind: "version_catalogue",
                 reference: "https://www.iso20022.org/iso-20022-message-definitions",
                 watching: "a new pain.001 version, which arrives yearly and changes which elements banks accept" } ],
             last_reviewed: "2026-09-15",
             next_review_due: "2027-09-15" }
end function

function adapter()
    return finio.adapter({
        id: "iso20022.pain001",
        revisions: revisions(),
        recognise: recognise,
        read: read_source,
        validate: validate_doc,
        write: write_doc,
        classify_write: classify_write,
        registry_entry: registry_entry(),
        byte_fidelity: true,
        semantic_fidelity: true })
end function
end library
