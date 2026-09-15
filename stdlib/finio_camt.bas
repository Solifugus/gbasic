' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' finio_camt -- ISO 20022 camt.053, a bank-to-customer statement, as a finio
' adapter. See docs/financial_adapters_design.md (§20's proving set).
'
' WHY THIS FORMAT SECOND, AND WHY NOT BAI2. §21's Phase 2 asks for "one
' fixed-width format; one hierarchical format; one additional representation",
' and §20's argument is that an abstraction surviving only one representation
' is a generalized parser rather than a description of the problem. BAI2 is
' record-oriented and legacy-heavy -- the SAME FAMILY as NACHA -- so building it
' second would exercise the identical layout-and-record path and settle
' nothing. camt is the one that pushes back.
'
' AND IT PUSHED BACK IMMEDIATELY, in the one place that matters: A HIERARCHICAL
' SOURCE HAS NO BYTE RANGE. `xml.parse` builds nodes carrying a name, a
' namespace, attributes and children and NO POSITION; the streaming reader
' carries a LINE, not an offset. So `finio.source_value`'s
' `{ record, byte_offset, byte_length }` cannot serve this adapter, and §4's
' "a universal byte-offset model is too representation-specific" stopped being
' a sentence and became `finio.location(kind, detail)`. What this adapter
' supplies is an XML location: a PATH and WHICH OCCURRENCE of it, because a
' statement with four hundred entries has four hundred elements at the same
' path and "it came from Ntry/Amt" identifies none of them.
'
' THE SECOND CONTRAST WITH NACHA IS THE REVISION, and it is the opposite
' answer. A NACHA file does not say which rule book produced it, so that
' adapter declares `unresolved` and says so. A camt document carries its
' version IN THE NAMESPACE -- urn:iso:std:iso:20022:tech:xsd:camt.053.001.08 --
' so the revision is DETERMINED FROM THE SOURCE, and a document declaring a
' version this adapter does not implement is refused BY NAME rather than read
' under the nearest one. Between them the two adapters cover both branches of
' §7's resolution diagram, which one adapter could not.
'
' WHAT THIS ADAPTER KNOWS AND HOW (Axiom 11). ISO 20022 publishes its message
' definitions and schemas openly, so unlike the NACHA Operating Rules there is
' a public specification to name -- which is why the registry entry below can
' say OPEN and `spec_obtained` where the other had to say DE_FACTO and
' `researched`. What it CANNOT say is `verified`: this adapter has never met a
' statement produced by a bank, and every document it has been run against was
' written here.
'
' BYTE FIDELITY IS DECLARED FALSE, and that is the first honest `false` in this
' tree. Re-serializing a parsed XML document does not reproduce its bytes --
' attribute order, whitespace and the declaration are all reconstructed -- so
' §17's weaker guarantee is the one available: reparsing the output yields
' equivalent financial meaning. The suite asserts BOTH halves, since an adapter
' claiming semantic fidelity and not providing it looks exactly the same.

library finio_camt

load finio from "finio.bas"
load xml

function namespace_prefix()
    return "urn:iso:std:iso:20022:tech:xsd:camt.053.001."
end function

' ONE REVISION, NAMED FROM THE NAMESPACE IT MATCHES. Declaring versions whose
' differences were not in hand would be the same invention the NACHA adapter
' refused to make about rule-book years -- so this names the one it was built
' against, and every other is refused by the framework with its own name in the
' message.
function revisions()
    return [ "camt.053.001.08" ]
end function

' --- §7 + Axiom 10: recognition -------------------------------------------
'
' NO PARSE. Recognition runs over every file in an archive sweep, and parsing
' each one to discover it is a purchase order would cost the whole scan. The
' namespace is a fixed string at a fixed place in the document's root element,
' so finding it is deterministic evidence obtained by looking.

function recognise(text)
    reasons = []
    if byte_count(text) = 0 then
        return { classification: "unknown", reasons: [ "the source is empty" ] }
    end if
    at = byte_find(text, namespace_prefix(), 0)
    if is_nothing(at) then
        return { classification: "unknown",
                 reasons: [ "no camt.053 namespace in this source" ] }
    end if
    ver = _version_at(text, at + byte_count(namespace_prefix()))
    if is_unknown(ver) then
        return { classification: "possible",
                 reasons: [ "the camt.053 namespace is present but carries no version number" ] }
    end if
    append(reasons, "the document namespace declares camt.053.001." + ver)
    rev = "camt.053.001." + ver
    ' THE ROOT ELEMENT IS THE SECOND HALF OF THE FINGERPRINT. The namespace
    ' string could appear in a document ABOUT camt -- a schema, a mapping
    ' table, an email -- and answering `exact` for one of those is how a scan
    ' comes to report an archive as full of statements it cannot read.
    if is_nothing(byte_find(text, "<Document", 0)) then
        append(reasons, "but there is no <Document> root element")
        return { classification: "possible", revision: rev, reasons: reasons }
    end if
    append(reasons, "and the root element is <Document>")
    if is_nothing(byte_find(text, "<BkToCstmrStmt", 0)) then
        append(reasons, "but no <BkToCstmrStmt>, which camt.053 requires")
        return { classification: "strong", revision: rev, reasons: reasons }
    end if
    append(reasons, "carrying <BkToCstmrStmt>")
    return { classification: "exact", revision: rev, reasons: reasons }
end function

function _version_at(text, pos)
    n = byte_count(text)
    out = ""
    i = pos
    while i < n
        c = byte_slice(text, i, 1)
        if c < "0" or c > "9" then
            i = n
        else
            out = out + c
            i = i + 1
        end if
    end while
    if byte_count(out) = 0 then
        return unknown
    end if
    return out
end function

' --- reading ---------------------------------------------------------------
'
' THE DOCUMENT SHAPE SURVIVES THE CHANGE OF REPRESENTATION, and that is the
' result §20 was asking for. `records` is still a flat list of interpreted
' things each carrying `kind` and `fields`, and `entities` is still the
' hierarchy expressed as indices into it -- the same two fields `finio_nacha`
' produces, so a consumer walking a document does not need to know which
' adapter made it.
'
' WHAT DOES NOT SURVIVE IS THE RECORD, and it should not. A fixed-width record
' carries `raw` and a byte range because it has them; an XML element does not,
' and putting a re-serialization there under the name `raw` would be a lie
' about where those bytes came from. So a camt record carries its PATH and
' OCCURRENCE instead. THE UNIFORM PART IS THE FIELD AND ITS LOCATION; the
' record is representation-shaped, which is true and was worth discovering.

function read_source(src, revision)
    doc = xml.parse(src.text)
    records = []
    loss = []
    stmts = []
    hdr = unknown
    gh = xml.find(doc, "BkToCstmrStmt/GrpHdr")
    if not is_unknown(gh) then
        hdr = count(records)
        append(records, { index: hdr, kind: "group_header",
                          path: "BkToCstmrStmt/GrpHdr", occurrence: 0,
                          fields: { message_id: _text_field(gh, "MsgId", "BkToCstmrStmt/GrpHdr/MsgId", 0),
                                    created: _text_field(gh, "CreDtTm", "BkToCstmrStmt/GrpHdr/CreDtTm", 0),
                                    recipient: _text_field(gh, "MsgRcpt/Nm", "BkToCstmrStmt/GrpHdr/MsgRcpt/Nm", 0) } })
    end if
    si = 0
    for each st in xml.find_all(doc, "BkToCstmrStmt/Stmt")
        base = "BkToCstmrStmt/Stmt"
        ccy = _text_of(st, "Acct/Ccy")
        stmt_index = count(records)
        append(records, { index: stmt_index, kind: "statement",
                          path: base, occurrence: si,
                          fields: { statement_id: _text_field(st, "Id", base + "/Id", si),
                                    created: _text_field(st, "CreDtTm", base + "/CreDtTm", si),
                                    account_iban: _text_field(st, "Acct/Id/IBAN", base + "/Acct/Id/IBAN", si),
                                    account_other: _text_field(st, "Acct/Id/Othr/Id", base + "/Acct/Id/Othr/Id", si),
                                    currency: _text_field(st, "Acct/Ccy", base + "/Acct/Ccy", si),
                                    owner: _text_field(st, "Acct/Ownr/Nm", base + "/Acct/Ownr/Nm", si),
                                    servicer_bic: _text_field(st, "Acct/Svcr/FinInstnId/BICFI", base + "/Acct/Svcr/FinInstnId/BICFI", si),
                                    from_date: _text_field(st, "FrToDt/FrDtTm", base + "/FrToDt/FrDtTm", si),
                                    to_date: _text_field(st, "FrToDt/ToDtTm", base + "/FrToDt/ToDtTm", si) } })
        balances = []
        bi = 0
        for each b in xml.find_all(st, "Bal")
            bpath = base + "/Bal"
            idx = count(records)
            append(records, { index: idx, kind: "balance", path: bpath, occurrence: bi,
                              fields: { code: _text_field(b, "Tp/CdOrPrtry/Cd", bpath + "/Tp/CdOrPrtry/Cd", bi),
                                        amount: _amount_field(b, "Amt", bpath + "/Amt", bi),
                                        indicator: _text_field(b, "CdtDbtInd", bpath + "/CdtDbtInd", bi),
                                        date: _text_field(b, "Dt/Dt", bpath + "/Dt/Dt", bi) } })
            append(balances, idx)
            bi = bi + 1
        end for
        summary = unknown
        sm = xml.find(st, "TxsSummry")
        if not is_unknown(sm) then
            spath = base + "/TxsSummry"
            summary = count(records)
            append(records, { index: summary, kind: "summary", path: spath, occurrence: si,
                              fields: { total_entries: _number_field(sm, "TtlNtries/NbOfNtries", spath + "/TtlNtries/NbOfNtries", si),
                                        credit_entries: _number_field(sm, "TtlCdtNtries/NbOfNtries", spath + "/TtlCdtNtries/NbOfNtries", si),
                                        credit_sum: _sum_field(sm, "TtlCdtNtries/Sum", spath + "/TtlCdtNtries/Sum", si, ccy),
                                        debit_entries: _number_field(sm, "TtlDbtNtries/NbOfNtries", spath + "/TtlDbtNtries/NbOfNtries", si),
                                        debit_sum: _sum_field(sm, "TtlDbtNtries/Sum", spath + "/TtlDbtNtries/Sum", si, ccy) } })
        end if
        entries = []
        ei = 0
        for each e in xml.find_all(st, "Ntry")
            epath = base + "/Ntry"
            idx = count(records)
            flds = { reference: _text_field(e, "NtryRef", epath + "/NtryRef", ei),
                     amount: _amount_field(e, "Amt", epath + "/Amt", ei),
                     indicator: _text_field(e, "CdtDbtInd", epath + "/CdtDbtInd", ei),
                     status: _text_field(e, "Sts/Cd", epath + "/Sts/Cd", ei),
                     booking_date: _text_field(e, "BookgDt/Dt", epath + "/BookgDt/Dt", ei),
                     value_date: _text_field(e, "ValDt/Dt", epath + "/ValDt/Dt", ei),
                     domain: _text_field(e, "BkTxCd/Domn/Cd", epath + "/BkTxCd/Domn/Cd", ei),
                     family: _text_field(e, "BkTxCd/Domn/Fmly/Cd", epath + "/BkTxCd/Domn/Fmly/Cd", ei),
                     end_to_end_id: _text_field(e, "NtryDtls/TxDtls/Refs/EndToEndId", epath + "/NtryDtls/TxDtls/Refs/EndToEndId", ei),
                     counterparty: _text_field(e, "NtryDtls/TxDtls/RltdPties/Cdtr/Pty/Nm", epath + "/NtryDtls/TxDtls/RltdPties/Cdtr/Pty/Nm", ei) }
            append(records, { index: idx, kind: "entry", path: epath, occurrence: ei, fields: flds })
            append(entries, idx)
            ei = ei + 1
        end for
        append(stmts, { statement: stmt_index, balances: balances,
                        summary: summary, entries: entries, currency: ccy })
        si = si + 1
    end for
    return { records: records,
             entities: { header: hdr, statements: stmts },
             loss: loss }
end function

' AXIOM 7 AT THE FIELD LEVEL, and in XML the distinction has a shape it does
' not have in a fixed-width record: an ABSENT ELEMENT is `unknown` -- the
' document said nothing, which for an optional element is ordinary and not a
' defect -- while a PRESENT element whose content cannot be what it claims is
' `invalid`. §18's rule that the token as written travels beside any mapped
' meaning is why `raw` is kept even where a typed value was produced.
function _text_field(node, path, full_path, occurrence)
    n = xml.find(node, path)
    loc = finio.location("xml", { path: full_path, occurrence: occurrence })
    if is_unknown(n) then
        return { status: "unknown", value: unknown, raw: unknown, location: loc }
    end if
    t = xml.text(n)
    if byte_count(trim(t)) = 0 then
        return { status: "unknown", value: unknown, raw: t, location: loc }
    end if
    return { status: "ok", value: trim(t), raw: t, location: loc }
end function

function _number_field(node, path, full_path, occurrence)
    f = _text_field(node, path, full_path, occurrence)
    if f.status != "ok" then
        return f
    end if
    if not _all_digits(f.value) then
        return { status: "invalid", value: unknown, raw: f.raw, location: f.location,
                 why: "expected a count, found '" + f.value + "'" }
    end if
    return { status: "ok", value: number(f.value), raw: f.raw, location: f.location }
end function

' THE CURRENCY IS AN ATTRIBUTE, NOT A FIELD, which is a shape a fixed-width
' format has no equivalent for: the amount and the unit it is denominated in
' live in different places in the document, and reading the number without the
' attribute yields a perfectly ordinary figure in no particular currency.
function _amount_field(node, path, full_path, occurrence)
    n = xml.find(node, path)
    loc = finio.location("xml", { path: full_path, occurrence: occurrence })
    if is_unknown(n) then
        return { status: "unknown", value: unknown, raw: unknown, location: loc, currency: unknown }
    end if
    t = trim(xml.text(n))
    ccy = xml.attr(n, "Ccy", "")
    if byte_count(t) = 0 then
        return { status: "unknown", value: unknown, raw: t, location: loc, currency: unknown }
    end if
    if byte_count(ccy) = 0 then
        return { status: "invalid", value: unknown, raw: t, location: loc, currency: unknown,
                 why: "the amount carries no Ccy attribute, so it is a number in no currency" }
    end if
    if not _is_decimal(t) then
        return { status: "invalid", value: unknown, raw: t, location: loc, currency: ccy,
                 why: "'" + t + "' is not a decimal amount" }
    end if
    ' EXACT, THROUGH `money`'s OWN TEXT PARSE. camt amounts are decimal text in
    ' the document, so there is no conversion to get wrong -- the text goes
    ' straight into the type that holds it exactly.
    m = _money(t, ccy)
    if is_unknown(m) then
        return { status: "invalid", value: unknown, raw: t, location: loc, currency: ccy,
                 why: "'" + ccy + "' is not a currency this build knows" }
    end if
    return { status: "ok", value: m, raw: t, location: loc, currency: ccy }
end function

' TWO AMOUNT-SHAPED THINGS IN ONE DOCUMENT, DENOMINATED DIFFERENTLY, and this
' is a distinction a fixed-width format cannot even express. An `<Amt>` carries
' its currency in a `Ccy` ATTRIBUTE and an amount without one is a number in no
' currency, which is a defect. A summary `<Sum>` carries NO attribute at all
' and is not meant to: it is a plain decimal whose currency is the account's,
' by definition of the element. Reading the second with the first's rule
' reports every well-formed summary as unreadable -- which is what the first
' draft did -- and reading the first with the second's rule would silently
' denominate a foreign amount in the account's currency, which is worse.
function _sum_field(node, path, full_path, occurrence, ccy)
    n = xml.find(node, path)
    loc = finio.location("xml", { path: full_path, occurrence: occurrence })
    if is_unknown(n) then
        return { status: "unknown", value: unknown, raw: unknown, location: loc, currency: unknown }
    end if
    t = trim(xml.text(n))
    if byte_count(t) = 0 then
        return { status: "unknown", value: unknown, raw: t, location: loc, currency: unknown }
    end if
    if byte_count(ccy) = 0 then
        return { status: "invalid", value: unknown, raw: t, location: loc, currency: unknown,
                 why: "this summary total has no currency of its own and the statement declares no account currency" }
    end if
    if not _is_decimal(t) then
        return { status: "invalid", value: unknown, raw: t, location: loc, currency: ccy,
                 why: "'" + t + "' is not a decimal amount" }
    end if
    m = _money(t, ccy)
    if is_unknown(m) then
        return { status: "invalid", value: unknown, raw: t, location: loc, currency: ccy,
                 why: "'" + ccy + "' is not a currency this build knows" }
    end if
    return { status: "ok", value: m, raw: t, location: loc, currency: ccy }
end function

function _money(text, ccy)
    on error goto next
    m = money.of(ccy, text)
    if error then
        error.clear()
        return unknown
    end if
    return m
end function

function _is_decimal(s)
    n = byte_count(s)
    if n = 0 then
        return false
    end if
    i = 0
    dots = 0
    digits = 0
    while i < n
        c = byte_slice(s, i, 1)
        if c = "." then
            dots = dots + 1
        else
            if c = "-" and i = 0 then
                i = i
            else
                if c < "0" or c > "9" then
                    return false
                end if
                digits = digits + 1
            end if
        end if
        i = i + 1
    end while
    return dots <= 1 and digits > 0
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

function _text_of(node, path)
    n = xml.find(node, path)
    if is_unknown(n) then
        return ""
    end if
    return trim(xml.text(n))
end function

' --- §15: validation -------------------------------------------------------
'
' THE STATEMENT'S OWN ARITHMETIC IS THE ORACLE, exactly as NACHA's control
' records are, differently expressed: a camt.053 statement carries an OPENING
' balance and a CLOSING one, and the entries between them must account for the
' difference. Those figures were computed by the bank that produced the file,
' so a reader that dropped an entry, read a credit as a debit or lost the
' currency disagrees with them immediately.
'
' PER STATEMENT AND NEVER ACROSS, which is why the fixture carries a second
' statement in a different currency: a reader totalling across statements
' produces a number that is the sum of dollars and euros, and `money` REFUSES
' to add different currencies -- so the mistake raises where it is made rather
' than travelling.

function validate_doc(doc)
    issues = []
    recs = doc.records
    for each st in doc.entities.statements
        sr = recs[st.statement]
        label = string(sr.fields.statement_id.value)
        ccy = st.currency
        opening = balance_of(recs, st, "OPBD")
        closing = balance_of(recs, st, "CLBD")
        if is_unknown(opening) then
            append(issues, { code: "missing_opening_balance", severity: "error",
                             record: st.statement,
                             message: "statement " + label + " carries no OPBD (opening) balance" })
        end if
        if is_unknown(closing) then
            append(issues, { code: "missing_closing_balance", severity: "error",
                             record: st.statement,
                             message: "statement " + label + " carries no CLBD (closing) balance" })
        end if
        t = entry_totals(recs, st)
        for each bad in t.unusable
            append(issues, bad)
        end for
        if not is_unknown(opening) and not is_unknown(closing) and t.usable then
            expected = opening.amount + t.credits - t.debits
            if expected != closing.amount then
                append(issues, { code: "balance_movement", severity: "error",
                                 record: st.statement,
                                 message: ("statement " + label + ": the opening balance "
                                           + string(opening.amount) + " plus " + string(t.credits)
                                           + " credited less " + string(t.debits) + " debited gives "
                                           + string(expected) + ", and the closing balance says "
                                           + string(closing.amount)),
                                 expected: string(expected), found: string(closing.amount) })
            end if
        end if
        ' The optional summary is a SECOND statement of the same facts, so a
        ' reader that summed the entries wrongly and a file whose summary is
        ' wrong are told apart -- and a reader agreeing with the balances while
        ' disagreeing with the summary has found something real.
        if not is_unknown(st.summary) then
            sm = recs[st.summary].fields
            append(issues, _compare(sm, "total_entries", count(st.entries), st.summary,
                "summary_entry_count", "statement " + label + ": the summary counts"))
            if t.usable then
                append(issues, _compare_money(sm, "credit_sum", t.credits, st.summary,
                    "summary_credit_total", "statement " + label + ": the summary's credit total is"))
                append(issues, _compare_money(sm, "debit_sum", t.debits, st.summary,
                    "summary_debit_total", "statement " + label + ": the summary's debit total is"))
            end if
        end if
        ' An entry denominated in a currency the account is not held in.
        for each ei in st.entries
            a = recs[ei].fields.amount
            if a.status = "ok" then
                if not is_unknown(a.currency) and byte_count(ccy) > 0 and a.currency != ccy then
                    append(issues, { code: "entry_currency", severity: "error", record: ei,
                                     concept: "amount",
                                     message: ("statement " + label + " is held in " + ccy
                                               + " and the entry at " + finio.describe_location(a.location)
                                               + " is in " + a.currency),
                                     expected: ccy, found: a.currency })
                end if
            end if
            ind = recs[ei].fields.indicator
            if ind.status != "ok" then
                append(issues, { code: "missing_indicator", severity: "error", record: ei,
                                 concept: "indicator",
                                 message: ("the entry at " + finio.describe_location(ind.location)
                                           + " has no CdtDbtInd, so its direction is unstated") })
            else
                if not contains([ "CRDT", "DBIT" ], ind.value) then
                    append(issues, { code: "unknown_indicator", severity: "error", record: ei,
                                     concept: "indicator",
                                     message: ("'" + string(ind.value) + "' is not CRDT or DBIT"),
                                     found: string(ind.value) })
                end if
            end if
        end for
    end for
    return _flatten(issues)
end function

' THE DIRECTION IS A SEPARATE ELEMENT FROM THE AMOUNT, which is the camt
' equivalent of NACHA's transaction code and goes wrong the same way: the sign
' is not in the number, so an entry read without its CdtDbtInd is a perfectly
' ordinary positive figure on whichever side the reader assumed.
function entry_totals(recs, st)
    credits = unknown
    debits = unknown
    unusable = []
    usable = true
    for each ei in st.entries
        f = recs[ei].fields
        a = f.amount
        ind = f.indicator
        if a.status != "ok" or ind.status != "ok" then
            usable = false
            append(unusable, { code: "unreadable_entry", severity: "error", record: ei,
                               message: ("the entry at " + finio.describe_location(a.location)
                                         + " cannot be totalled: "
                                         + _why_of(a) + _why_of(ind)) })
        else
        if byte_count(st.currency) > 0 and not is_unknown(a.currency) and a.currency != st.currency then
            ' AN ENTRY IN ANOTHER CURRENCY IS EXCLUDED FROM THE TOTAL AND
            ' REPORTED, never added. `money` REFUSES to add different
            ' currencies -- which is the right guard and is why this defect
            ' cannot silently produce a nonsense total -- but a validator that
            ' let that refusal propagate would DIE on the malformed statement
            ' instead of describing it, and §15 says a malformed source is
            ' preserved as far as safely possible with its problems reported.
            ' The currency mismatch itself is raised by the caller; what this
            ' does is decline to be the thing that stops the walk.
            usable = false
            append(unusable, { code: "uncountable_entry", severity: "error", record: ei,
                               message: ("the entry at " + finio.describe_location(a.location)
                                         + " is in " + string(a.currency) + " and the statement is in "
                                         + st.currency + ", so it is in neither total") })
        else
            if ind.value = "CRDT" then
                if is_unknown(credits) then
                    credits = a.value
                else
                    credits = credits + a.value
                end if
            end if
            if ind.value = "DBIT" then
                if is_unknown(debits) then
                    debits = a.value
                else
                    debits = debits + a.value
                end if
            end if
        end if
        end if
    end for
    ' A statement with no credits at all still has a credit total, and it is
    ' zero IN THE STATEMENT'S OWN CURRENCY -- `money` has no currency-free zero,
    ' so it is built from the account's.
    if is_unknown(credits) then
        credits = _zero(st.currency)
    end if
    if is_unknown(debits) then
        debits = _zero(st.currency)
    end if
    return { credits: credits, debits: debits, usable: usable, unusable: unusable }
end function

function _zero(ccy)
    if byte_count(ccy) = 0 then
        return unknown
    end if
    return money.of(ccy, "0")
end function

function _why_of(f)
    if has(f, "why") then
        return string(f.why) + " "
    end if
    if f.status = "unknown" then
        return "the element is absent "
    end if
    return ""
end function

function balance_of(recs, st, code)
    for each bi in st.balances
        f = recs[bi].fields
        if f.code.status = "ok" and f.code.value = code then
            if f.amount.status = "ok" then
                return { amount: f.amount.value, record: bi,
                         indicator: f.indicator.value, date: f.date.value }
            end if
        end if
    end for
    return unknown
end function

function _compare(fields, concept, want, record_index, code, phrase)
    got = fields[concept]
    if got.status != "ok" then
        return [ { code: code, severity: "error", record: record_index, concept: concept,
                   message: phrase + " nothing readable; the entries give " + string(want),
                   expected: string(want), found: string(got.raw) } ]
    end if
    if got.value = want then
        return []
    end if
    return [ { code: code, severity: "error", record: record_index, concept: concept,
               message: phrase + " " + string(got.value) + "; the entries give " + string(want),
               expected: string(want), found: string(got.value) } ]
end function

function _compare_money(fields, concept, want, record_index, code, phrase)
    got = fields[concept]
    if got.status != "ok" then
        return [ { code: code, severity: "error", record: record_index, concept: concept,
                   message: phrase + " nothing readable; the entries give " + string(want),
                   expected: string(want), found: string(got.raw) } ]
    end if
    if got.value = want then
        return []
    end if
    return [ { code: code, severity: "error", record: record_index, concept: concept,
               message: phrase + " " + string(got.value) + "; the entries give " + string(want),
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
' SEMANTIC FIDELITY, NOT BYTE FIDELITY, and the adapter DECLARES which. A
' parsed XML document cannot be re-serialized to the bytes it came from --
' attribute order, insignificant whitespace and the declaration are all
' reconstructed -- so §17's stronger guarantee is simply not available here,
' where it is available for NACHA and is claimed. Saying `byte_fidelity: true`
' and re-serializing anyway would produce a document that re-reads correctly
' and does not match the file an auditor has, which is the difference §17 was
' written to keep visible. THE SOURCE IS STILL RETAINED (Axiom 1), so the bytes
' that arrived are never lost -- only the ability to reproduce them from the
' model.

' RE-PARSED FROM THE RETAINED SOURCE, not from a tree the document carried.
' The framework builds a document out of `records`, `entities` and `loss` and
' drops anything else an adapter returns, and it is right to: A DOCUMENT THAT
' CANNOT BE STORED IS NOT A DOCUMENT. A live libxml2 tree is a handle, not a
' value -- `encode` refuses one, it cannot cross `spawn`, and a document held
' between two HTTP requests would come back holding nothing. That is the same
' rule `agent` reached from the other direction when its run had to keep
' `tools.schema` instead of a toolset.
'
' AXIOM 1 IS WHAT MAKES IT FREE: the source is retained, so the writer can
' always reconstruct what it needs. The cost is a parse per write, which is
' what correctness costs here.
'
' NOTE for when this adapter gains an editor: NACHA rewrites a record's `raw`
' in place and writing replays it, which works because the bytes ARE the model.
' Here the bytes are not, so an edit will have to be recorded on the document
' and replayed against the reparse. That is deliberately not built: there is no
' editor yet, and a mechanism with no caller is a mechanism nothing checks.
function write_doc(doc)
    return { text: xml.encode(xml.parse(doc.source.text), false),
             loss: [ finio.loss_note("narrowed",
                       { why: ("this document was re-serialized from its parsed form: "
                               + "attribute order, insignificant whitespace and the XML "
                               + "declaration are reconstructed, so the bytes differ from "
                               + "the source even where every value is preserved") }) ] }
end function

' --- the adapter -----------------------------------------------------------

function registry_entry()
    return { id: "iso20022.camt053",
             name: "ISO 20022 camt.053 bank-to-customer statement",
             family: "cash management",
             domain: "bank account statements",
             authority: "ISO 20022 Registration Management Group",
             description: "XML statement: a group header, then one statement per account carrying the account's identification and currency, opening and closing balances, an optional transaction summary, and one entry per booked movement with its amount, currency, direction and dates.",
             representation: "hierarchical XML, namespaced by message version",
             transport: "file or message, delivered by the account servicer",
             known_revisions: [ "camt.053.001.08" ],
             specification_sources: [ "ISO 20022 message definitions and schemas published at iso20022.org" ],
             ' OPEN, WHERE NACHA HAD TO SAY DE_FACTO. ISO 20022 publishes its
             ' message definitions and schemas without charge, so there is a
             ' specification to name -- which is the first time an entry in
             ' this tree can honestly claim one.
             acquisition_class: "OPEN",
             spec_public: true,
             implementation_allowed: true,
             spec_redistribution_allowed: false,
             sample_redistribution_allowed: false,
             ' `spec_obtained` AND NOT FURTHER. The specification is public and
             ' citable; this adapter was nonetheless written from the message
             ' structure rather than against a schema validator, and it has
             ' never met a statement produced by a bank. `verified` is what
             ' that last fact rules out, and the distinction is the entire
             ' reason §9 has five states rather than a boolean.
             state: "spec_obtained",
             recognition_status: "implemented",
             read_status: "implemented",
             write_status: "re-serialization only; this adapter does not originate a statement",
             validation_status: "implemented",
             test_vectors: [ "tests/finio/camt/*.xml -- written here, not produced by a bank" ],
             known_variants: [],
             known_extensions: [],
             last_reviewed: "2026-09-14",
             next_review_due: "2027-09-14" }
end function

function adapter()
    return finio.adapter({
        id: "iso20022.camt053",
        revisions: revisions(),
        recognise: recognise,
        read: read_source,
        validate: validate_doc,
        write: write_doc,
        registry_entry: registry_entry(),
        byte_fidelity: false,
        semantic_fidelity: true })
end function
end library
