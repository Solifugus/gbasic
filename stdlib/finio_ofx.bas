' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' finio_ofx -- OFX (Open Financial Exchange), the FOURTH finio adapter.
' See docs/financial_adapters_design.md §20.
'
' §20's SCHEMA-DRIFT CASE, OCCURRING INSIDE ONE FORMAT. OFX 1.x is SGML-like
' and NOT well-formed XML -- a leaf element is `<CODE>0` with no closing tag --
' while 2.x is proper XML with `</CODE>`. Same element tree, two serializations,
' and a file of each kind is ordinary. That is the situation §20 says a format
' abstraction must survive, and it is the first time it has arrived within a
' single adapter rather than between two.
'
' ONE READER FOR BOTH, AND CONVERSION WAS REJECTED. The obvious approach is to
' insert the missing closing tags and hand the result to `xml.parse`, which is
' what more than one public tool does. It is refused here for a reason that is
' not taste: THE LOCATIONS WOULD POINT INTO TEXT THE BANK NEVER SENT. Axiom 2
' says an interpreted value is traceable to its source, and a byte offset into
' a document this library invented is not provenance. So the reader walks the
' tag stream of the ORIGINAL bytes, and the only difference between the
' dialects is whether a leaf's closing tag is there to be skipped.
'
' THE RULE THAT MAKES ONE READER POSSIBLE: a closing tag whose name does not
' match the innermost open aggregate is a LEAF'S closing tag and is ignored.
' In 1.x there are none; in 2.x every leaf has one. Nothing else differs.
'
' OFX HAS NO CONTROL TOTAL, and that is worth stating because the three formats
' before it all had one and it would have been easy to generalise. NACHA states
' hashes and totals, camt states an opening and closing balance, BAI2 states a
' three-level checksum -- OFX states a ledger balance that is a BALANCE and not
' a sum of anything in the file. So validation here cannot reconcile arithmetic,
' and what it can do instead is check the things OFX consumers actually depend
' on: that the response is not an ERROR, and that FITIDs are UNIQUE.
'
' THE FITID IS THE ONE THAT MATTERS. It exists to let a consumer recognise a
' transaction it has already seen, and every piece of accounting software that
' imports OFX dedups on it. A duplicate means a transaction is silently dropped
' or counted twice, and neither shows up as an error anywhere.
'
' WHAT IT KNOWS AND HOW (Axiom 11). The OFX specification is published by the
' Financial Data Exchange and CARRIES AN EXPLICIT ROYALTY-FREE, WORLDWIDE,
' PERPETUAL LICENCE TO IMPLEMENT -- which is Axiom 12 answered in writing rather
' than inferred, and is the only format in this registry that does so. The
' element names below were read from files published by real financial
' institutions (ANZ, Suncorp, Fidelity, and others) in the ofxparse project's
' MIT-licensed fixture corpus.

library finio_ofx

load finio from "finio.bas"

function dialects()
    return [ "sgml", "xml" ]
end function

' THE VERSIONS ACTUALLY READ, not a range claimed. The corpus carries 102, 200, 203
' and 211; a file declaring anything else is refused BY NAME rather than read
' under the nearest, which is the rule the camt adapter follows for the same
' reason -- the bank-statement element tree is stable across these, and saying
' so about versions nobody here has seen would be an invention.
function revisions()
    return [ "102", "200", "203", "211" ]
end function

' --- §7 + Axiom 10: recognition -------------------------------------------

function recognise(text)
    reasons = []
    if byte_count(text) = 0 then
        return { classification: "unknown", reasons: [ "the source is empty" ] }
    end if
    ' A 2.x file declares its header in an `<?OFX ...?>` processing
    ' instruction; a 1.x file declares it in plain KEY:VALUE lines before the
    ' markup. LEADING BLANK LINES ARE ORDINARY -- one real fixture opens with
    ' twelve of them -- so nothing here depends on the header being first.
    pi = byte_find(text, "<?OFX", 0)
    if not is_nothing(pi) then
        ver = _attr_after(text, pi, "VERSION")
        append(reasons, "an <?OFX?> processing instruction declares version " + string(ver))
        if is_nothing(byte_find(text, "<OFX>", 0)) then
            append(reasons, "but there is no <OFX> element")
            return { classification: "possible", dialect: "xml", reasons: reasons }
        end if
        return { classification: "exact", revision: ver, dialect: "xml",
                 reasons: reasons }
    end if
    h = byte_find(text, "OFXHEADER:", 0)
    if is_nothing(h) then
        return { classification: "unknown",
                 reasons: [ "no <?OFX?> processing instruction and no OFXHEADER: line" ] }
    end if
    ver = _header_value(text, "VERSION:")
    data = _header_value(text, "DATA:")
    append(reasons, "an OFXHEADER: block declares version " + string(ver) + " and data " + string(data))
    if is_nothing(byte_find(text, "<OFX>", 0)) then
        append(reasons, "but there is no <OFX> element")
        return { classification: "possible", dialect: "sgml", reasons: reasons }
    end if
    if data != "OFXSGML" then
        append(reasons, "and DATA is not OFXSGML, which 1.x declares")
        return { classification: "strong", revision: ver, dialect: "sgml", reasons: reasons }
    end if
    return { classification: "exact", revision: ver, dialect: "sgml", reasons: reasons }
end function

function _header_value(text, key)
    at = byte_find(text, key, 0)
    if is_nothing(at) then
        return ""
    end if
    i = at + byte_count(key)
    n = byte_count(text)
    out = ""
    while i < n
        c = byte_slice(text, i, 1)
        if c = chr(10) or c = chr(13) then
            i = n
        else
            out = out + c
            i = i + 1
        end if
    end while
    return trim(out)
end function

function _attr_after(text, from_at, name)
    at = byte_find(text, name + "=", from_at)
    if is_nothing(at) then
        return ""
    end if
    i = at + byte_count(name) + 1
    if byte_slice(text, i, 1) = chr(34) then
        i = i + 1
    end if
    n = byte_count(text)
    out = ""
    while i < n
        c = byte_slice(text, i, 1)
        if c = chr(34) or c = " " or c = "?" then
            i = n
        else
            out = out + c
            i = i + 1
        end if
    end while
    return out
end function

' --- the tag stream --------------------------------------------------------
'
' One walk over the ORIGINAL bytes, producing an event per element with the
' path it sits at and the byte where it begins. Both dialects are the same
' stream; only the leaf closing tags differ, and a closing tag that does not
' match the innermost open aggregate is one of those.

function scan(text)
    events = []
    stack = []
    seen = {}
    n = byte_count(text)
    i = 0
    while i < n
        lt = byte_find(text, "<", i)
        if is_nothing(lt) then
            i = n
        else
            gt = byte_find(text, ">", lt)
            if is_nothing(gt) then
                i = n
            else
                tag = byte_slice(text, lt + 1, gt - lt - 1)
                head = byte_slice(tag, 0, 1)
                if head = "?" or head = "!" then
                    i = gt + 1
                else
                    if head = "/" then
                        name = trim(byte_slice(tag, 1, byte_count(tag) - 1))
                        if count(stack) > 0 and stack[count(stack) - 1] = name then
                            ' An aggregate closing.
                            append(events, { kind: "close", name: name,
                                             path: _path(stack), at: lt })
                            stack = _pop(stack)
                        end if
                        ' OTHERWISE IT IS A LEAF'S CLOSING TAG -- 2.x writes one
                        ' and 1.x does not, and that single difference is the
                        ' whole of the dialect split. Ignoring it here is what
                        ' lets one reader serve both without rewriting the
                        ' source, which would move every location off the bytes
                        ' the bank actually sent.
                        i = gt + 1
                    else
                        name = trim(tag)
                        nxt = byte_find(text, "<", gt + 1)
                        stop_at = n
                        if not is_nothing(nxt) then
                            stop_at = nxt
                        end if
                        value = byte_slice(text, gt + 1, stop_at - gt - 1)
                        path = _path(stack) + "/" + name
                        occ = 0
                        if has(seen, path) then
                            occ = seen[path]
                        end if
                        seen[path] = occ + 1
                        if byte_count(trim(value)) > 0 then
                            append(events, { kind: "leaf", name: name, path: path,
                                             occurrence: occ, value: trim(value),
                                             raw: value, at: lt })
                        else
                            append(events, { kind: "open", name: name, path: path,
                                             occurrence: occ, at: lt })
                            append(stack, name)
                        end if
                        i = gt + 1
                    end if
                end if
            end if
        end if
    end while
    return events
end function

function _path(stack)
    return join(stack, "/")
end function

function _pop(stack)
    out = []
    i = 0
    while i < count(stack) - 1
        append(out, stack[i])
        i = i + 1
    end while
    return out
end function

' --- reading ---------------------------------------------------------------
'
' The document shape is the one the other three adapters produce: a flat
' `records` list of interpreted things carrying `kind` and `fields`, and an
' `entities` hierarchy of indices into it. LOCATIONS ARE THE `xml` KIND,
' because that is a statement about SHAPE rather than syntax -- OFX is a tagged
' tree whichever way it is serialized, and a path with an occurrence is what
' identifies an element in one.

function read_source(src, revision)
    events = scan(src.text)
    records = []
    loss = []
    collecting = []
    for each e in events
        if e.kind = "open" then
            if _starts_collector(e.name) then
                ' WHERE ITS DESCENDANTS WILL START. An aggregate closes AFTER
                ' everything inside it, so at close time its children are
                ' exactly the records created since it opened -- which is how
                ' containment is determined here rather than by close order.
                append(collecting, { name: e.name, path: e.path,
                                     occurrence: e.occurrence, fields: {},
                                     first_child: count(records) })
            end if
        end if
        if e.kind = "leaf" then
            if count(collecting) > 0 then
                k = count(collecting) - 1
                collecting[k].fields[e.name] = { status: "ok", value: e.value, raw: e.raw,
                                                 location: finio.location("xml",
                                                     { path: e.path, occurrence: e.occurrence }) }
            end if
        end if
        if e.kind = "close" then
            if count(collecting) > 0 then
                k = count(collecting) - 1
                if collecting[k].name = e.name then
                    c = collecting[k]
                    collecting = _drop_last(collecting)
                    append(records, { index: count(records), kind: _kind_of(c.name),
                                      element: c.name, path: c.path,
                                      occurrence: c.occurrence, fields: c.fields,
                                      first_child: c.first_child })
                end if
            end if
        end if
    end for
    return { records: records, entities: _entities(records), loss: loss }
end function

' CONTAINMENT IS AN INDEX RANGE, NOT CLOSE ORDER. A transaction closes BEFORE
' the statement that holds it -- innermost first -- so a single pass that
' attached each record to "the current statement" attaches every transaction to
' nothing at all, because the statement does not exist yet. The first draft did
' exactly that and read ten real files with ZERO transactions, reporting every
' one of them CLEAN: an empty statement and a healthy one look identical, which
' is the failure this whole library is built to avoid. A record's descendants
' are the records created between its collector opening and closing, and that
' is known at close time.
function _entities(records)
    signon = unknown
    statements = []
    i = 0
    while i < count(records)
        r = records[i]
        if r.kind = "signon" then
            signon = i
        end if
        if r.kind = "statement" then
            st = { statement: i, transactions: [], balances: [],
                   account: unknown, status: unknown, period: unknown }
            j = r.first_child
            while j < i
                child = records[j]
                if child.kind = "transaction" then
                    append(st.transactions, j)
                end if
                if child.kind = "balance" then
                    append(st.balances, j)
                end if
                if child.kind = "account" then
                    st.account = j
                end if
                if child.kind = "transaction_list" then
                    st.period = j
                end if
                if child.kind = "status" and is_unknown(st.status) then
                    st.status = j
                end if
                j = j + 1
            end while
            append(statements, st)
        end if
        i = i + 1
    end while
    return { signon: signon, statements: statements }
end function

function _starts_collector(name)
    return contains([ "SONRS", "STMTRS", "CCSTMTRS", "STMTTRN", "LEDGERBAL",
                      "AVAILBAL", "BANKACCTFROM", "CCACCTFROM", "STATUS",
                      "BANKTRANLIST" ], name)
end function

function _kind_of(name)
    if name = "SONRS" then
        return "signon"
    end if
    if name = "STMTRS" or name = "CCSTMTRS" then
        return "statement"
    end if
    if name = "STMTTRN" then
        return "transaction"
    end if
    if name = "LEDGERBAL" or name = "AVAILBAL" then
        return "balance"
    end if
    if name = "BANKACCTFROM" or name = "CCACCTFROM" then
        return "account"
    end if
    if name = "BANKTRANLIST" then
        return "transaction_list"
    end if
    if name = "STATUS" then
        return "status"
    end if
    return "unknown"
end function

function _drop_last(items)
    out = []
    i = 0
    while i < count(items) - 1
        append(out, items[i])
        i = i + 1
    end while
    return out
end function

' --- §15: validation -------------------------------------------------------
'
' OFX STATES NO CONTROL TOTAL, so this cannot reconcile arithmetic the way the
' other three adapters do. What it checks instead is what OFX consumers
' actually depend on.
'
' THE STATUS IS FIRST AND IT IS THE ONE THAT MATTERS MOST. A response carrying
' a non-zero code is an ERROR, and a reader that walks past it finds an empty
' or partial statement and reports a balance of nothing -- which looks exactly
' like an account with no activity. Two files in the corpus are error
' responses, and one of them still carries a zero status at the signon level,
' so the check has to look at every status rather than the first.
'
' THE FITID IS THE SECOND. It exists so a consumer can recognise a transaction
' it has already imported, and every piece of accounting software that reads
' OFX dedups on it. A duplicate means a transaction is silently dropped or
' counted twice, and nothing anywhere reports it.

function validate_doc(doc)
    issues = []
    recs = doc.records
    for each r in recs
        if r.kind = "status" then
            c = r.fields
            if has(c, "CODE") then
                if c.CODE.value != "0" then
                    sev = "error"
                    msg = ""
                    if has(c, "MESSAGE") then
                        msg = " -- " + string(c.MESSAGE.value)
                    end if
                    append(issues, { code: "status_not_ok", severity: sev, record: r.index,
                                     concept: "CODE",
                                     message: ("the response at " + finio.describe_location(c.CODE.location)
                                               + " carries status code " + string(c.CODE.value)
                                               + ", not 0" + msg),
                                     found: string(c.CODE.value), expected: "0" })
                end if
            else
                append(issues, { code: "status_missing_code", severity: "error", record: r.index,
                                 message: "a STATUS aggregate carries no CODE" })
            end if
        end if
    end for
    ' A TRANSACTION THAT BELONGS TO NO STATEMENT IS REPORTED, and this is the
    ' check that catches a structure this adapter does not model rather than a
    ' file that is wrong. `fidelity-savings.ofx` carries four transactions
    ' inside an INVSTMTRS investment statement: they are read, they are attached
    ' to nothing, and without this the file reports a clean bank statement with
    ' no activity -- which is indistinguishable from an account that had none.
    ' It is written as "belongs to no statement" rather than as a test for
    ' INVSTMTRS because the next unmodelled structure should be reported too,
    ' by the check that already exists.
    attached = {}
    for each st in doc.entities.statements
        for each ti in st.transactions
            attached[string(ti)] = true
        end for
    end for
    orphans = 0
    first_orphan = unknown
    for each r in recs
        if r.kind = "transaction" then
            if not has(attached, string(r.index)) then
                orphans = orphans + 1
                if is_unknown(first_orphan) then
                    first_orphan = r.index
                end if
            end if
        end if
    end for
    if orphans > 0 then
        append(issues, { code: "unmodelled_statement", severity: "error",
                         record: first_orphan,
                         message: (string(orphans) + " transaction(s) sit inside a statement kind this adapter does not interpret"
                                   + " -- their amounts are in no statement, and a reader taking this file as a bank statement"
                                   + " sees an account with no activity"),
                         found: string(orphans) })
    end if
    for each st in doc.entities.statements
        seen = {}
        for each ti in st.transactions
            f = recs[ti].fields
            if has(f, "FITID") then
                id = string(f.FITID.value)
                if has(seen, id) then
                    append(issues, { code: "duplicate_fitid", severity: "error", record: ti,
                                     concept: "FITID",
                                     message: ("FITID '" + id + "' appears more than once in this statement ("
                                               + finio.describe_location(f.FITID.location)
                                               + "). A consumer deduplicating on it will drop one of them."),
                                     found: id })
                else
                    seen[id] = ti
                end if
            else
                append(issues, { code: "missing_fitid", severity: "error", record: ti,
                                 message: ("the transaction at " + string(ti)
                                           + " carries no FITID, so a consumer cannot tell it from one it has already imported") })
            end if
            if not has(f, "TRNAMT") then
                append(issues, { code: "missing_amount", severity: "error", record: ti,
                                 message: "a transaction carries no TRNAMT" })
            else
                if not _is_decimal(f.TRNAMT.value) then
                    append(issues, { code: "amount_not_decimal", severity: "error", record: ti,
                                     concept: "TRNAMT",
                                     message: ("'" + string(f.TRNAMT.value) + "' is not a signed decimal amount"),
                                     found: string(f.TRNAMT.value) })
                end if
            end if
            ' DTPOSTED MUST FALL INSIDE THE PERIOD THE LIST DECLARES. A
            ' transaction outside it is not an error in the format's grammar and
            ' is one in its meaning: a consumer importing by date range will
            ' either miss it or import it twice.
            if not is_unknown(st.period) and has(f, "DTPOSTED") then
                p = recs[st.period].fields
                if has(p, "DTSTART") and has(p, "DTEND") then
                    d = _date8(f.DTPOSTED.value)
                    s0 = _date8(p.DTSTART.value)
                    s1 = _date8(p.DTEND.value)
                    if byte_count(d) = 8 and byte_count(s0) = 8 and byte_count(s1) = 8 then
                        if d < s0 or d > s1 then
                            append(issues, { code: "posted_outside_period", severity: "warning",
                                             record: ti, concept: "DTPOSTED",
                                             message: ("a transaction posted " + d
                                                       + " sits outside the list's declared period "
                                                       + s0 + ".." + s1),
                                             found: d })
                        end if
                    end if
                end if
            end if
        end for
    end for
    return issues
end function

function _date8(v)
    if byte_count(v) < 8 then
        return ""
    end if
    return byte_slice(v, 0, 8)
end function

function _is_decimal(s)
    n = byte_count(s)
    if n = 0 then
        return false
    end if
    i = 0
    digits = 0
    dots = 0
    while i < n
        c = byte_slice(s, i, 1)
        if c = "." then
            dots = dots + 1
        else
            if (c = "-" or c = "+") and i = 0 then
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

' --- §16 + §17: writing ----------------------------------------------------
'
' THE SOURCE IS RETAINED AND RE-EMITTED, which is what makes byte fidelity
' achievable for a format whose two dialects could not otherwise survive a
' round trip: re-serializing a parsed 1.x document would produce 2.x, and a
' reader downstream expecting what the bank sent would get something else.
function write_doc(doc)
    return { text: doc.source.text, loss: [] }
end function

' --- the adapter -----------------------------------------------------------

function registry_entry()
    return { id: "ofx",
             name: "OFX (Open Financial Exchange)",
             family: "account aggregation",
             domain: "consumer and small-business statement download and bank aggregation",
             authority: "Financial Data Exchange (FDX), which took over the OFX consortium's specification in 2019",
             description: "A tagged document carrying a signon response and one or more statement responses, each with an account, a transaction list bounded by a declared period, and ledger and available balances. SGML-like in 1.x and XML in 2.x.",
             representation: "tagged document; SGML-like in 1.x with no closing tag on a leaf, XML in 2.x",
             transport: "HTTP request/response, or a downloaded file",
             known_revisions: [ "102", "103", "151", "160", "200", "202", "203", "211" ],
             specification_sources: [
               { source_type: "specification",
                 source_url_or_reference: "OFX 2.2 specification (https://financialdataexchange.org/wp-content/uploads/2025/12/OFX-2.2.pdf) and the OFX downloads page (https://ofxorg.ocg-prod.a.intuit.com/downloads.html)",
                 date_retrieved: "2026-09-15",
                 note: "A ROYALTY-FREE, WORLDWIDE, PERPETUAL LICENCE is granted to any party to use the specification to make, use and sell products -- Axiom 12 answered in writing rather than inferred, and the only format in this registry that does so. The download PAGES were seen; the PDF was not retrieved." },
               { source_type: "public_sample",
                 source_url_or_reference: "https://github.com/jseutter/ofxparse tests/fixtures -- files published by real financial institutions",
                 date_retrieved: "2026-09-15",
                 note: "MIT-licensed corpus carrying BOTH DIALECTS and real bank output (ANZ, Suncorp, Fidelity and others). Read BEFORE this adapter was written, which is why one reader serves both dialects rather than one being retrofitted." } ],
             acquisition_class: "OPEN",
             spec_public: true,
             spec_acquisition_method: "Free download; the specification carries an explicit royalty-free implementation licence.",
             implementation_allowed: true,
             spec_redistribution_allowed: false,
             sample_redistribution_allowed: false,
             state: "researched",
             recognition_status: "implemented",
             read_status: "implemented",
             write_status: "the retained source is re-emitted; this adapter does not originate a statement",
             validation_status: "implemented",
             test_vectors: [
               { name: "tests/finio/foreign_ofx/*.ofx",
                 source_url_or_reference: "https://github.com/jseutter/ofxparse tests/fixtures",
                 date_retrieved: "2026-09-15",
                 licence: "MIT, Copyright Jerry Seutter and contributors",
                 usage: "permitted", redistribution: "permitted",
                 note: "redistributed with the licence alongside. TEN FILES covering both dialects, real bank output, an empty document, two ERROR RESPONSES, and one opening with twelve blank lines before its header." } ],
             known_variants: [
               "1.x is SGML-like and not well-formed XML, so an XML parser cannot read it at all -- §20's schema drift occurring inside one format",
               "vendor extension tags with a dot in the name, e.g. INTU.BID and INTU.USERID from Intuit",
               "leading blank lines before the header: one real fixture opens with twelve",
               "an entirely empty document: <OFX></OFX> with no signon and no status" ],
             known_extensions: [
               "investment statements (INVSTMTRS) and 401k positions are present in the corpus and are NOT interpreted by this adapter; their records are read as unknown aggregates" ],
             maintenance_priority: "periodic",
             watch_sources: [
               { kind: "document",
                 reference: "https://financialdataexchange.org/wp-content/uploads/2025/12/OFX-2.2.pdf",
                 watching: "a version beyond 2.2, or a change to the royalty-free implementation licence" } ],
             last_reviewed: "2026-09-15",
             next_review_due: "2027-09-15" }
end function

function adapter()
    return finio.adapter({
        id: "ofx",
        revisions: revisions(),
        recognise: recognise,
        read: read_source,
        validate: validate_doc,
        write: write_doc,
        registry_entry: registry_entry(),
        byte_fidelity: true,
        semantic_fidelity: true })
end function
end library
