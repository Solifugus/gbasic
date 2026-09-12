' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' finio PHASE 0's ONE MEASUREMENT (docs/financial_adapters_design.md §21).
'
' Axiom 2 says every interpreted value is traceable to the source it came from.
' A 100,000-record file at eleven fields is 1.1 MILLION source locations, and
' whether gBASIC carries that comfortably is NOT KNOWABLE BY READING. It decides
' between three architectures -- not three settings:
'
'   per_value   provenance held for every field         (1.1M structures)
'   per_record  provenance held once per record, field  (100k structures)
'               locations derived from a shared layout
'   on_demand   nothing stored; the SOURCE is retained
'               and a location is computed when asked
'
' ALL THREE HOLD THE INTERPRETED VALUES, because every consumer needs those.
' They differ in PROVENANCE alone, which is what makes the comparison mean
' something.
'
' BUILD COST ALONE WOULD RECOMMEND on_demand AND THE RECOMMENDATION WOULD BE
' WRONG: its cost is at QUERY time, and a consumer that asks "where did this
' come from" for every value it reports pays that cost per value. So the
' experiment asks both questions, and the answer is a TRADE rather than a
' winner. Time is measured from OUTSIDE (gBASIC's clock is second-resolution)
' and peak memory by /usr/bin/time, so this program prints only what it DID.

program main( args )
    mode = "per_value"
    path = "nacha.txt"
    queries = 1000
    if count(args) > 0 then
        mode = args[0]
    end if
    if count(args) > 1 then
        path = args[1]
    end if
    if count(args) > 2 then
        queries = number(args[2])
    end if

    layout = _layout()
    src {file}= path
    text = read(src)
    lines = split(text, chr(10))

    ' Only the entry detail records (type 6) carry the eleven fields.
    detail = []
    offsets = []
    pos = 0
    for each ln in lines
        if starts_with(ln, "6") and len(ln) >= 94 then
            append(detail, ln)
            append(offsets, pos)
        end if
        pos = pos + len(ln) + 1
    end for

    if mode = "per_value" then
        store = _build_per_value(detail, offsets, layout)
    else
        if mode = "per_record" then
            store = _build_per_record(detail, offsets, layout)
        else
            if mode = "on_demand" then
                store = _build_on_demand(detail, offsets, layout)
            else
                print "unknown mode: " + mode
                return 1
            end if
        end if
    end if

    print "MODE " + mode
    print "RECORDS " + string(count(detail))
    print "FIELDS " + string(count(layout))
    print "LOCATIONS " + string(count(detail) * count(layout))
    print "BUILT"

    ' The query half. The same questions in every mode, chosen by a fixed
    ' stride so the three answer an identical workload.
    answered = 0
    checksum = 0
    q = 0
    nrec = count(detail)
    nfld = count(layout)
    while q < queries
        r = mod(q * 7919, nrec)
        fidx = mod(q * 31, nfld)
        if mode = "per_value" then
            got = _query_per_value(store, r, fidx, nfld)
        else
            if mode = "per_record" then
                got = _query_per_record(store, layout, r, fidx)
            else
                got = _query_on_demand(store, layout, r, fidx)
            end if
        end if
        checksum = checksum + got.byte_offset
        answered = answered + 1
        q = q + 1
    end while
    print "QUERIED " + string(answered)
    print "CHECKSUM " + string(checksum)
    return 0
end program

' --- the NACHA entry detail layout, shared by every mode -------------------
' ONE copy, whatever the record count: a layout is a property of the FORMAT,
' not of the data, which is the whole reason per_record can be cheaper.
function _layout()
    return [ { concept: "record_type",    offset: 0,  length: 1 },
             { concept: "transaction_code", offset: 1, length: 2 },
             { concept: "receiving_dfi",  offset: 3,  length: 8 },
             { concept: "check_digit",    offset: 11, length: 1 },
             { concept: "account_number", offset: 12, length: 17 },
             { concept: "amount",         offset: 29, length: 10 },
             { concept: "individual_id",  offset: 39, length: 15 },
             { concept: "individual_name", offset: 54, length: 22 },
             { concept: "discretionary",  offset: 76, length: 2 },
             { concept: "addenda_flag",   offset: 78, length: 1 },
             { concept: "trace_number",   offset: 79, length: 15 } ]
end function

' --- A: provenance per value ----------------------------------------------
' §4's SourceValue and §5's SemanticValue, in full, for every field. sources[]
' is an ARRAY because §5 says the relationship is many-to-many.
function _build_per_value(detail, offsets, layout)
    out = []
    r = 0
    n = count(detail)
    nf = count(layout)
    while r < n
        ln = detail[r]
        base = offsets[r]
        f = 0
        while f < nf
            spec = layout[f]
            raw = mid(ln, spec.offset, spec.length)
            append(out, { concept: spec.concept,
                          value: trim(raw),
                          sources: [ { raw: raw,
                                       location: { record: r,
                                                   byte_offset: base + spec.offset,
                                                   byte_length: spec.length },
                                       format: "nacha",
                                       revision: "2021" } ],
                          transformations: [ "trim" ] })
            f = f + 1
        end while
        r = r + 1
    end while
    return out
end function

function _query_per_value(store, r, fidx, nfld)
    sv = store[(r * nfld) + fidx]
    return { raw: sv.sources[0].raw,
             record: sv.sources[0].location.record,
             byte_offset: sv.sources[0].location.byte_offset,
             byte_length: sv.sources[0].location.byte_length }
end function

' --- B: provenance per record ---------------------------------------------
' One location per RECORD; a field's location is that plus the layout's own
' offset. The values are still held per field, since a consumer needs them.
function _build_per_record(detail, offsets, layout)
    rows = []
    r = 0
    n = count(detail)
    nf = count(layout)
    while r < n
        ln = detail[r]
        vals = {}
        f = 0
        while f < nf
            spec = layout[f]
            vals[spec.concept] = trim(mid(ln, spec.offset, spec.length))
            f = f + 1
        end while
        append(rows, { values: vals,
                       location: { record: r,
                                   byte_offset: offsets[r],
                                   byte_length: 94 },
                       format: "nacha",
                       revision: "2021" })
        r = r + 1
    end while
    return rows
end function

function _query_per_record(store, layout, r, fidx)
    row = store[r]
    spec = layout[fidx]
    return { raw: row.values[spec.concept],
             record: row.location.record,
             byte_offset: row.location.byte_offset + spec.offset,
             byte_length: spec.length }
end function

' --- C: reconstructed on demand from a retained source ---------------------
' NOTHING per record. The source text and the record offsets are retained, and
' a location is computed when asked -- which is why the query half of this
' experiment exists: on build cost alone this mode wins by construction.
function _build_on_demand(detail, offsets, layout)
    return { lines: detail, offsets: offsets }
end function

function _query_on_demand(store, layout, r, fidx)
    ln = store.lines[r]
    spec = layout[fidx]
    return { raw: mid(ln, spec.offset, spec.length),
             record: r,
             byte_offset: store.offsets[r] + spec.offset,
             byte_length: spec.length }
end function
