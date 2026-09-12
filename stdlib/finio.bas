' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' finio -- the financial adapter framework's VALUE MODEL.
' See docs/financial_adapters_design.md. This is PHASE 0 and deliberately
' contains NO ADAPTER: what it fixes is the shape everything above it will be
' built on, and §21 says that shape is decided by a measurement rather than by
' preference. See examples/finio_lab/provenance_cost.bas for the measurement and
' §21 for its result.
'
' THE ARCHITECTURE IS "RECONSTRUCTED ON DEMAND FROM A RETAINED SOURCE", and it
' was not chosen because it is elegant -- it was measured against the two
' alternatives at the design's own scale (100,000 records, 1.1M source
' locations) and it wins on memory by 9x and 52x while costing about two
' microseconds a query. Holding provenance per VALUE needs 2.79 GB for a 9.5 MB
' file and is not merely expensive, it is also the SLOWEST to answer.

library finio

' --- §4: source ------------------------------------------------------------
'
' The source is RETAINED, which is Axiom 1 and is also what makes provenance
' computable rather than stored. `records` are the byte offsets at which each
' record begins, so a location is arithmetic and not a search.

function open_text(text, options)
    opts = _options(options, [ "format", "revision", "origin" ], "finio.open_text")
    lines = []
    offsets = []
    pos = 0
    for each ln in split(text, chr(10))
        append(lines, ln)
        append(offsets, pos)
        pos = pos + len(ln) + 1
    end for
    return { text: text,
             lines: lines,
             offsets: offsets,
             format: _required(opts, "format", "finio.open_text"),
             revision: _required(opts, "revision", "finio.open_text"),
             origin: _default(opts, "origin", "") }
end function

' --- §4: a fixed-width layout ----------------------------------------------
'
' ONE copy, whatever the record count: a layout is a property of the FORMAT and
' not of the data, which is precisely why per-record and per-value storage buy
' nothing a shared layout does not already give.

function layout(fields)
    if type(fields) != "array" then
        error "finio.layout expects an array of field records"
    end if
    if count(fields) = 0 then
        error "finio.layout expects at least one field"
    end if
    seen = []
    out = []
    for each f in fields
        checked = _options(f, [ "concept", "offset", "length" ], "finio.layout")
        c = _required(f, "concept", "finio.layout field")
        if contains(seen, c) then
            error "finio.layout: the concept '" + c + "' appears twice"
        end if
        append(seen, c)
        o = _required(f, "offset", "finio.layout field")
        l = _required(f, "length", "finio.layout field")
        if o < 0 then
            error "finio.layout: the offset of '" + c + "' is negative"
        end if
        if l <= 0 then
            error "finio.layout: the length of '" + c + "' must be greater than zero"
        end if
        append(out, { concept: c, offset: o, length: l })
    end for
    return out
end function

function concepts(lay)
    out = []
    for each f in lay
        append(out, f.concept)
    end for
    return out
end function

' --- §4 + §5: provenance, computed ----------------------------------------
'
' AXIOM 6, NEVER SILENTLY GUESS: a concept the layout does not define is
' refused BY NAME rather than answered with nothing, because a caller that
' mistyped one would otherwise receive an ordinary-looking empty value.

function source_value(src, lay, record_index, concept)
    spec = _spec(lay, concept)
    if record_index < 0 or record_index >= count(src.lines) then
        error "finio.source_value: no record " + string(record_index) + " in this source"
    end if
    ln = src.lines[record_index]
    raw = mid(ln, spec.offset, spec.length)
    return { raw: raw,
             location: { record: record_index,
                         byte_offset: src.offsets[record_index] + spec.offset,
                         byte_length: spec.length },
             format: src.format,
             revision: src.revision }
end function

' §5's SemanticValue. `sources` is an ARRAY because the design states the
' relationship is many-to-many: a later revision may compose one concept from
' several source fields, and an older one may split a single field into
' several concepts.

function semantic_value(concept, value, sources, transformations)
    if type(sources) != "array" then
        error "finio.semantic_value expects sources to be an array"
    end if
    if count(sources) = 0 then
        error "finio.semantic_value: a value with no source has no provenance (Axiom 2)"
    end if
    if type(transformations) != "array" then
        error "finio.semantic_value expects transformations to be an array"
    end if
    return { concept: concept,
             value: value,
             sources: sources,
             transformations: transformations }
end function

' --- Axiom 7: unknown is DIFFERENT FROM invalid ---------------------------
'
' A blank field is UNKNOWN -- the source said nothing. A field that is present
' and fails its own rule is INVALID -- the source said something that cannot be
' what it claims. Collapsing them either way loses the distinction a reviewer
' needs: one is a gap to be filled, the other is a defect to be reported.

function read_field(src, lay, record_index, concept, kind)
    sv = source_value(src, lay, record_index, concept)
    body = trim(sv.raw)
    if len(body) = 0 then
        return { status: "unknown", value: unknown, raw: sv.raw, source: sv }
    end if
    if kind = "text" then
        return { status: "ok", value: body, raw: sv.raw, source: sv }
    end if
    if kind = "digits" then
        i = 0
        while i < len(body)
            ch = mid(body, i, 1)
            if ch < "0" or ch > "9" then
                return { status: "invalid", value: unknown, raw: sv.raw,
                         reason: "expected digits, found '" + ch + "'", source: sv }
            end if
            i = i + 1
        end while
        return { status: "ok", value: number(body), raw: sv.raw, source: sv }
    end if
    error "finio.read_field: unknown field kind '" + string(kind) + "'"
end function

' --- §9: the format registry ----------------------------------------------
'
' THE FIVE STATES MUST REMAIN DISTINCT, and that is the point of checking them:
' "finding that a format exists is not equivalent to possessing enough
' legitimate information to implement it". An entry that claims `implemented`
' without a specification source is refused, because that claim is exactly the
' one a registry exists to keep honest.

function registry_states()
    return [ "discovered", "spec_obtained", "researched", "implemented", "verified" ]
end function

function acquisition_classes()
    return [ "OPEN", "CONTROLLED", "PUBLIC_VENDOR", "DE_FACTO", "HUMAN_REQUIRED", "INSUFFICIENT" ]
end function

function check_registry_entry(entry)
    known = [ "id", "name", "family", "domain", "authority", "description",
              "representation", "transport", "known_revisions", "effective_dates",
              "specification_sources", "acquisition_class", "spec_public",
              "implementation_allowed", "spec_redistribution_allowed",
              "sample_redistribution_allowed", "state", "recognition_status",
              "read_status", "write_status", "validation_status",
              "test_vectors", "known_variants", "known_extensions",
              "last_reviewed", "next_review_due" ]
    checked = _options(entry, known, "finio.check_registry_entry")
    id = _required(entry, "id", "finio.check_registry_entry")
    st = _required(entry, "state", "finio.check_registry_entry")
    if not contains(registry_states(), st) then
        error ("finio.check_registry_entry: '" + id + "' has state '" + string(st) + "', which is not one of " + join(registry_states(), ", "))
    end if
    ac = _required(entry, "acquisition_class", "finio.check_registry_entry")
    if not contains(acquisition_classes(), ac) then
        error ("finio.check_registry_entry: '" + id + "' has acquisition_class '" + string(ac) + "', which is not one of " + join(acquisition_classes(), ", "))
    end if
    ' A state BEYOND spec_obtained claims a specification is in hand.
    needs_spec = [ "spec_obtained", "researched", "implemented", "verified" ]
    if contains(needs_spec, st) then
        srcs = _default(entry, "specification_sources", [])
        if count(srcs) = 0 then
            error ("finio.check_registry_entry: '" + id + "' is '" + st + "' and names no specification_sources -- the state claims a spec is held")
        end if
    end if
    ' §12/Axiom 12: implementing a format nobody is allowed to implement is a
    ' legal problem, not a technical one, and the registry is where it is seen.
    if contains([ "implemented", "verified" ], st) then
        if _default(entry, "implementation_allowed", unknown) != true then
            error ("finio.check_registry_entry: '" + id + "' is '" + st + "' without implementation_allowed being true (Axiom 12)")
        end if
    end if
    return true
end function

' --- §12: adapter provenance ----------------------------------------------
'
' A DIFFERENT QUESTION FROM DATA PROVENANCE: not "where did this value come
' from" but "why does the adapter believe this element has this meaning". The
' chain is required in full, because a rule that cannot name its evidence is
' the thing this framework exists not to produce.

function adapter_rule(rule)
    checked = _options(rule, [ "behavior", "rule", "evidence", "spec_revision",
                     "authority", "retrieved" ], "finio.adapter_rule")
    for each field in [ "behavior", "rule", "evidence", "spec_revision", "authority", "retrieved" ]
        v = _required(rule, field, "finio.adapter_rule")
    end for
    return rule
end function

' --- helpers ---------------------------------------------------------------

function _spec(lay, concept)
    for each f in lay
        if f.concept = concept then
            return f
        end if
    end for
    error ("finio: this layout defines no concept '" + string(concept) + "' -- it has " + join(concepts(lay), ", "))
end function

' The rule webserver.listen already follows: an unrecognised field is refused
' BY NAME. A typo that is ignored is indistinguishable from a deliberate
' omission, which is how a Context typo once produced `materiality: unknown`
' and raised nothing.
function _options(rec, known, label)
    if type(rec) != "record" then
        error label + " expects a record"
    end if
    for each k in keys(rec)
        if not contains(known, k) then
            error label + ": unknown field '" + k + "' (known: " + join(known, ", ") + ")"
        end if
    end for
    return rec
end function

function _required(rec, field, label)
    if not has(rec, field) then
        error label + " requires '" + field + "'"
    end if
    return rec[field]
end function

function _default(rec, field, fallback)
    if not has(rec, field) then
        return fallback
    end if
    return rec[field]
end function
end library
