' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' finio -- the financial adapter framework.
' See docs/financial_adapters_design.md.
'
' PHASE 0 fixed the VALUE MODEL -- the source, the layout, provenance, and the
' format registry -- and §21 says that shape was decided by a MEASUREMENT
' rather than by preference. See examples/finio_lab/provenance_cost.bas for it
' and §21 for the result.
'
' PHASE 1 is the framework on top: adapters as values, recognition, the
' resolver, the read/validate split, and the loss primitives. The first adapter
' is `finio_nacha`; Phase 1 and it shipped together because a framework whose
' only consumer is imaginary cannot be tested at all.
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
' computable rather than stored. `offsets` are the byte offsets at which each
' record begins, so a location is arithmetic and not a search.
'
' OFFSETS AND SLICES ARE BYTES, NEVER CODEPOINTS, and that is a CORRECTION to
' Phase 0 rather than a refinement of it. Phase 0 accumulated offsets with `len`
' and sliced with `mid`, both of which count CODEPOINTS, so the field it called
' `byte_offset` stopped being one the moment a record carried a non-ASCII byte
' -- and for a FIXED-WIDTH format the extraction itself moves: one UTF-8
' E-acute inside a 22-byte name field shifts every later field one place left,
' and a 15-digit trace number comes back as an ORDINARY-LOOKING 13-digit one
' with nothing raised. Phase 0's suite could not see it because its fixture is
' pure ASCII. That is PLAT-NUL's standing lesson one library along: a defect in
' how one place reads a string is evidence about every place that does.
'
' FRAMING IS NOT AN ASSUMPTION EITHER. A great many real fixed-width files --
' NACHA among them -- arrive with NO record separator at all, as one blocked
' run of 94-byte records straight off a mainframe, and a reader that assumes
' newlines reads such a file as a single enormous record. Which framing a
' source has is part of RECOGNITION; `open_text` is TOLD, and records both what
' it was told and the separator each record actually carried, so a writer can
' reproduce the file it read (§17 byte fidelity) rather than a normalised
' version of it.

function open_text(text, options)
    opts = _options(options, [ "format", "revision", "origin", "framing", "record_length" ], "finio.open_text")
    framing = _default(opts, "framing", "lines")
    record_length = _default(opts, "record_length", 0)
    records = []
    offsets = []
    terminators = []
    total = byte_count(text)
    if framing = "fixed" then
        if record_length <= 0 then
            error "finio.open_text: framing 'fixed' requires a record_length greater than zero"
        end if
        pos = 0
        while pos < total
            take = record_length
            if pos + take > total then
                take = total - pos
            end if
            append(records, byte_slice(text, pos, take))
            append(offsets, pos)
            append(terminators, "")
            pos = pos + take
        end while
    else
        if framing != "lines" then
            error ("finio.open_text: framing '" + string(framing) + "' is not one of lines, fixed")
        end if
        pos = 0
        while pos < total
            ' `byte_find` MISSES WITH `nothing`, NOT `unknown`, and
            ' `is_unknown(nothing)` is FALSE -- an is_unknown guard here reads
            ' as "found at index nothing" and the arithmetic below then fails
            ' on a file that simply has no line feed in it, which is exactly
            ' the blocked file this branch exists to handle.
            nl = byte_find(text, chr(10), pos)
            if is_nothing(nl) then
                append(records, byte_slice(text, pos, total - pos))
                append(offsets, pos)
                append(terminators, "")
                pos = total
            else
                body = byte_slice(text, pos, nl - pos)
                term = chr(10)
                if byte_count(body) > 0 and byte_slice(body, byte_count(body) - 1, 1) = chr(13) then
                    body = byte_slice(body, 0, byte_count(body) - 1)
                    term = chr(13) + chr(10)
                end if
                append(records, body)
                append(offsets, pos)
                append(terminators, term)
                pos = nl + 1
            end if
        end while
    end if
    return { text: text,
             records: records,
             offsets: offsets,
             terminators: terminators,
             framing: framing,
             record_length: record_length,
             format: _required(opts, "format", "finio.open_text"),
             revision: _required(opts, "revision", "finio.open_text"),
             origin: _default(opts, "origin", "") }
end function

' §17: keeping what arrived is the floor, so a source can always give back the
' bytes it was handed. A writer that means to preserve a file it did not change
' has something to compare against that is not its own output.
function source_text(src)
    return src.text
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

' --- §4: a location is a VALUE WITH A KIND ---------------------------------
'
' "A universal byte-offset model is too representation-specific. The framework
' should instead provide a generic source-location abstraction." §4 said that
' from the beginning and Phase 1 did not honour it: every location this library
' could make was `{ record, byte_offset, byte_length }`, because the only
' adapter was fixed-width and nothing pushed back.
'
' MEASURED, AND THAT IS WHY THIS IS HERE NOW: a hierarchical source cannot
' supply a byte range at all. `xml.parse` builds nodes carrying name, namespace,
' attributes and children and NO POSITION; the streaming reader carries a LINE,
' not an offset. So an XML adapter asked for `byte_offset` has three choices --
' invent one, answer `unknown`, or say where it really is -- and only the third
' is provenance. The kinds below are §4's own list.
'
' THE KIND IS REQUIRED AND THE FIELDS ARE CHECKED PER KIND, because a location
' whose shape depends on who built it is one a consumer has to guess at, and a
' guess about where a value came from is the thing this framework exists not to
' produce.

function location_kinds()
    return [ "fixed_width", "delimited", "spreadsheet", "xml", "json" ]
end function

function _location_required(kind)
    if kind = "fixed_width" then
        return [ "record", "byte_offset", "byte_length" ]
    end if
    if kind = "delimited" then
        return [ "row", "column" ]
    end if
    if kind = "spreadsheet" then
        return [ "sheet", "row", "column" ]
    end if
    if kind = "xml" then
        ' A path and WHICH OCCURRENCE of it. The path alone is not a location:
        ' a statement with four hundred entries has four hundred elements at
        ' the same path, and "it came from Ntry/Amt" identifies none of them.
        return [ "path", "occurrence" ]
    end if
    if kind = "json" then
        return [ "json_pointer" ]
    end if
    error ("finio.location: '" + string(kind) + "' is not one of " + join(location_kinds(), ", "))
end function

function _location_optional(kind)
    if kind = "spreadsheet" then
        return [ "cell" ]
    end if
    if kind = "xml" then
        ' A line is what the streaming reader can give and the DOM parser
        ' cannot, so it is optional rather than required -- an adapter that has
        ' one says so, and one that does not is not made to invent it.
        return [ "line", "byte_offset", "byte_length" ]
    end if
    return []
end function

function location(kind, detail)
    req = _location_required(kind)
    known = req
    for each o in _location_optional(kind)
        append(known, o)
    end for
    checked = _options(detail, known, "finio.location(" + string(kind) + ")")
    out = { kind: kind }
    for each f in req
        out[f] = _required(detail, f, "finio.location(" + string(kind) + ")")
    end for
    for each f in _location_optional(kind)
        if has(detail, f) then
            out[f] = detail[f]
        end if
    end for
    return out
end function

' §18 wants provenance a consumer can act on, and a location it cannot print is
' harder to act on than one it can. One renderer, so a report mixing sources of
' different representations reads consistently rather than each adapter
' inventing a phrasing.
function describe_location(loc)
    k = _required(loc, "kind", "finio.describe_location")
    if k = "fixed_width" then
        return ("record " + string(loc.record) + ", bytes " + string(loc.byte_offset)
                + ".." + string(loc.byte_offset + loc.byte_length - 1))
    end if
    if k = "delimited" then
        return "row " + string(loc.row) + ", column " + string(loc.column)
    end if
    if k = "spreadsheet" then
        if has(loc, "cell") then
            return string(loc.sheet) + "!" + string(loc.cell)
        end if
        return string(loc.sheet) + " row " + string(loc.row) + ", column " + string(loc.column)
    end if
    if k = "xml" then
        out = string(loc.path) + "[" + string(loc.occurrence) + "]"
        if has(loc, "line") then
            out = out + " (line " + string(loc.line) + ")"
        end if
        return out
    end if
    if k = "json" then
        return string(loc.json_pointer)
    end if
    error ("finio.describe_location: unknown location kind '" + string(k) + "'")
end function

' --- §4 + §5: provenance, computed ----------------------------------------
'
' AXIOM 6, NEVER SILENTLY GUESS: a concept the layout does not define is
' refused BY NAME rather than answered with nothing, because a caller that
' mistyped one would otherwise receive an ordinary-looking empty value.

function source_value(src, lay, record_index, concept)
    spec = _spec(lay, concept)
    if record_index < 0 or record_index >= count(src.records) then
        error "finio.source_value: no record " + string(record_index) + " in this source"
    end if
    ln = src.records[record_index]
    raw = byte_slice(ln, spec.offset, spec.length)
    return { raw: raw,
             location: location("fixed_width",
                                { record: record_index,
                                  byte_offset: src.offsets[record_index] + spec.offset,
                                  byte_length: spec.length }),
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
    if byte_count(body) = 0 then
        return { status: "unknown", value: unknown, raw: sv.raw, source: sv }
    end if
    if kind = "text" then
        return { status: "ok", value: body, raw: sv.raw, source: sv }
    end if
    if kind = "digits" then
        ' BY BYTE, like everything else in this library. A codepoint loop
        ' reaches the same verdict here, since any non-ASCII lead byte sorts
        ' above "9" -- but "the same rule everywhere" is the point of the
        ' correction, and one loop left counting codepoints is where the next
        ' reader learns the wrong rule. The position is reported rather than
        ' the character, because a single byte of a multi-byte character is
        ' not a character and printing it produces mojibake in a diagnostic.
        i = 0
        while i < byte_count(body)
            b = byte_at(body, i)
            if b < 48 or b > 57 then
                return { status: "invalid", value: unknown, raw: sv.raw,
                         reason: ("expected digits; byte " + string(i) + " of the trimmed field is " + string(b)),
                         source: sv }
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

' A SOURCE IS A RECORD, NOT A SENTENCE, and that is §9 read as it is written:
' `source_type`, `source_url_or_reference` and `date_retrieved` are indented
' UNDER `specification_sources[]`, so they are the fields of each source rather
' than three more top-level strings. Phase 1 accepted a bare sentence and the
' validator refused all three names, so a discovery pass could not have
' recorded what evidence it used -- which is precisely what §14 asks of it
' ("record exactly what evidence was used").
'
' A RETRIEVAL DATE IS REQUIRED, and it is the field that makes the rest
' falsifiable: a URL with no date is a claim about a page as it is today, and
' §13's whole maintenance story is that specifications move.

function source_types()
    return [ "specification", "vendor_documentation", "standards_body",
             "regulator", "public_sample", "implementation_guide",
             "observed_production_data" ]
end function

function check_specification_source(src, label)
    checked = _options(src, [ "source_type", "source_url_or_reference",
                              "date_retrieved", "note" ], label)
    t = _required(src, "source_type", label)
    if not contains(source_types(), t) then
        error (label + ": source_type '" + string(t) + "' is not one of " + join(source_types(), ", "))
    end if
    r = _required(src, "source_url_or_reference", label)
    d = _required(src, "date_retrieved", label)
    return src
end function

function check_registry_entry(entry)
    known = [ "id", "name", "family", "domain", "authority", "description",
              "representation", "transport", "known_revisions", "effective_dates",
              "specification_sources", "acquisition_class", "spec_public",
              "spec_acquisition_method",
              "implementation_allowed", "spec_redistribution_allowed",
              "sample_redistribution_allowed", "state", "recognition_status",
              "read_status", "write_status", "validation_status",
              "test_vectors", "known_variants", "known_extensions",
              "blocked_by", "last_reviewed", "next_review_due" ]
    ' §9 ALSO LISTS `implementation_status`, AND IT IS NOT ADDED. It names the
    ' same fact as `state`, whose five values §9 itself then enumerates, and two
    ' fields for one fact is the drift this tree keeps finding rather than a
    ' completeness win. `state` IS §9's implementation_status, renamed once.
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
    srcs = _default(entry, "specification_sources", [])
    if type(srcs) != "array" then
        error ("finio.check_registry_entry: '" + id + "' gives a " + type(srcs) + " for specification_sources, which must be an array")
    end if
    n = 0
    for each src in srcs
        checked_src = check_specification_source(src, "finio.check_registry_entry: '" + id + "' specification_source " + string(n))
        n = n + 1
    end for
    if contains(needs_spec, st) then
        if count(srcs) = 0 then
            error ("finio.check_registry_entry: '" + id + "' is '" + st + "' and names no specification_sources -- the state claims a spec is held")
        end if
    end if
    ' AN ENTRY THAT CANNOT BE IMPLEMENTED MUST SAY WHY, because §10's whole
    ' point is that automated research has "a clear stopping point" and can
    ' report `Format discovered. Implementation blocked. Human acquisition
    ' required.` An INSUFFICIENT or HUMAN_REQUIRED entry with no reason is
    ' indistinguishable from one nobody has looked at.
    if contains([ "INSUFFICIENT", "HUMAN_REQUIRED" ], ac) then
        if byte_count(string(_default(entry, "blocked_by", ""))) = 0 then
            error ("finio.check_registry_entry: '" + id + "' is " + ac + " and says nothing in blocked_by -- §10 requires a stopping point that names what is missing")
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

' ===========================================================================
' PHASE 1: THE FRAMEWORK (§21)
' ===========================================================================
'
' --- §7: an adapter is a VALUE, and so is the set of them -----------------
'
' THE REGISTRY IS A VALUE THE CALLER HOLDS, not a global this library mutates,
' and that is a decision rather than a convenience. gBASIC's actors are
' fork+exec, so a registration performed in a parent is not the child's; a
' global registry would work in a script and quietly hold nothing in a worker,
' which is the shape of defect this whole framework exists not to produce. A
' value is also what lets two registries coexist -- one pinned to the adapters
' an archive was imported under, one carrying today's -- which §13's
' maintenance story needs and a singleton cannot express.
'
' The cost is that §7's `finio.read("payments.ach")` takes a registry argument.
' That is the whole cost, and it is paid once per call site.

function adapter(spec)
    known = [ "id", "revisions", "recognise", "read", "validate", "write",
              "registry_entry", "byte_fidelity", "semantic_fidelity" ]
    checked = _options(spec, known, "finio.adapter")
    for each field in [ "id", "revisions", "recognise", "read", "registry_entry", "byte_fidelity" ]
        v = _required(spec, field, "finio.adapter")
    end for
    if type(spec.revisions) != "array" or count(spec.revisions) = 0 then
        error "finio.adapter: '" + string(spec.id) + "' must name at least one revision"
    end if
    for each field in [ "recognise", "read" ]
        if type(spec[field]) != "function" then
            error ("finio.adapter: '" + string(spec.id) + "' gives a " + type(spec[field]) + " for '" + field + "', which must be a function")
        end if
    end for
    ' §17: every adapter documents which round-trip guarantee it can provide.
    ' Leaving it unstated is how a consumer comes to assume the stronger one.
    if type(spec.byte_fidelity) != "boolean" then
        error ("finio.adapter: '" + string(spec.id) + "' must state byte_fidelity as true or false (§17)")
    end if
    ' Axiom 11/12: the adapter's own provenance is checked where it is declared,
    ' not where it is first doubted.
    checked_entry = check_registry_entry(spec.registry_entry)
    return spec
end function

function registry(adapters)
    if type(adapters) != "array" then
        error "finio.registry expects an array of adapters"
    end if
    seen = []
    for each a in adapters
        if contains(seen, a.id) then
            error "finio.registry: the adapter '" + a.id + "' is registered twice"
        end if
        append(seen, a.id)
    end for
    return { adapters: adapters, ids: seen }
end function

function find_adapter(reg, id)
    for each a in reg.adapters
        if a.id = id then
            return a
        end if
    end for
    error ("finio: no adapter '" + string(id) + "' in this registry -- it holds " + join(reg.ids, ", "))
end function

' --- §7 + Axiom 10: recognition -------------------------------------------
'
' THE CLASSIFICATION VOCABULARY IS ORDINAL AND DELIBERATELY NOT NUMERIC. §7
' says to avoid implying statistical certainty where the evidence is
' deterministic: a file either carries the 094/10/1 fingerprint or it does not,
' and dressing that as "confidence 0.91" claims a measurement nobody made --
' which is the correction insight.weigh had to make one library over.

function classifications()
    return [ "exact", "strong", "possible", "ambiguous", "unknown" ]
end function

function identify(reg, text)
    candidates = []
    for each a in reg.adapters
        fn = a.recognise
        r = fn(text)
        if not contains(classifications(), r.classification) then
            error ("finio.identify: adapter '" + a.id + "' answered with classification '" + string(r.classification) + "', which is not one of " + join(classifications(), ", "))
        end if
        if r.classification != "unknown" then
            append(candidates, { adapter: a.id,
                                 revision: _default(r, "revision", unknown),
                                 classification: r.classification,
                                 framing: _default(r, "framing", "lines"),
                                 record_length: _default(r, "record_length", 0),
                                 reasons: _default(r, "reasons", []) })
        end if
    end for
    if count(candidates) = 0 then
        return { classification: "unknown", candidates: [],
                 why: "no registered adapter recognised this source" }
    end if
    if count(candidates) = 1 then
        return { classification: candidates[0].classification, candidates: candidates,
                 why: candidates[0].adapter + " recognised it and no other adapter did" }
    end if
    ' AXIOM 6 IN ITS SHARPEST FORM: several adapters claim the source, so the
    ' framework reports AMBIGUOUS and names them. Picking the strongest claim
    ' would be a plausible answer and is exactly the guess this refuses -- two
    ' adapters both answering `exact` disagree about a fact, and the resolution
    ' is evidence the framework does not have.
    return { classification: "ambiguous", candidates: candidates,
             why: ("more than one adapter recognised this source: " + _candidate_list(candidates)) }
end function

function _candidate_list(candidates)
    out = []
    for each c in candidates
        append(out, c.adapter + " (" + c.classification + ")")
    end for
    return join(out, ", ")
end function

' --- §7: the resolver -----------------------------------------------------
'
' The order §7 gives: an explicit adapter, then an as-of hint, then
' deterministic recognition, then the candidate set, then an error. `read`
' RAISES where it cannot resolve rather than returning a document interpreted
' under a guess, because a document is the thing every consumer downstream
' trusts and a guessed one is indistinguishable from a read one.

function read_text(reg, text, options)
    opts = _options(options, [ "adapter", "revision", "asof", "framing", "record_length", "origin" ], "finio.read_text")
    chosen = unknown
    revision = _default(opts, "revision", unknown)
    classification = "exact"
    reasons = []
    framing = _default(opts, "framing", unknown)
    record_length = _default(opts, "record_length", unknown)
    if has(opts, "adapter") then
        chosen = find_adapter(reg, opts.adapter)
        append(reasons, "the caller pinned adapter '" + opts.adapter + "'")
        if is_unknown(framing) then
            fn = chosen.recognise
            r = fn(text)
            framing = _default(r, "framing", "lines")
            record_length = _default(r, "record_length", 0)
        end if
    else
        ident = identify(reg, text)
        if ident.classification = "unknown" then
            error ("finio.read_text: no registered adapter recognised this source -- the registry holds " + join(reg.ids, ", ") + ". Pin one with { adapter: ... } if you know what it is.")
        end if
        if ident.classification = "ambiguous" then
            error ("finio.read_text: " + ident.why + ". Pin one with { adapter: ... } -- a plausible reading is not chosen silently (Axiom 6).")
        end if
        c = ident.candidates[0]
        chosen = find_adapter(reg, c.adapter)
        classification = c.classification
        append(reasons, "recognised as " + c.adapter + " (" + c.classification + ")")
        for each why in c.reasons
            append(reasons, why)
        end for
        if is_unknown(revision) then
            revision = c.revision
        end if
        if is_unknown(framing) then
            framing = c.framing
            record_length = c.record_length
        end if
    end if
    if is_unknown(revision) then
        ' AN ADAPTER MAY HONESTLY NOT KNOW. §7's diagram assumes a revision can
        ' always be settled from evidence, and for a format whose record layout
        ' has outlived twenty rule books it cannot: the file simply does not say
        ' which year's rules produced it. The adapter's FIRST declared revision
        ' is used and the reason SAYS SO, rather than the framework inventing a
        ' year that would then travel in every document it wrote.
        revision = chosen.revisions[0]
        append(reasons, "the source carries no revision evidence; read under '" + string(revision) + "', the adapter's default")
    end if
    if not contains(chosen.revisions, revision) then
        error ("finio.read_text: adapter '" + chosen.id + "' does not implement revision '" + string(revision) + "' -- it has " + join(chosen.revisions, ", "))
    end if
    if is_unknown(framing) then
        framing = "lines"
    end if
    if is_unknown(record_length) then
        record_length = 0
    end if
    src = open_text(text, { format: chosen.id, revision: revision,
                            framing: framing, record_length: record_length,
                            origin: _default(opts, "origin", "") })
    fn = chosen.read
    body = fn(src, revision)
    return { adapter: chosen.id,
             revision: revision,
             classification: classification,
             reasons: reasons,
             source: src,
             records: _default(body, "records", []),
             entities: _default(body, "entities", {}),
             loss: _default(body, "loss", []),
             byte_fidelity: chosen.byte_fidelity }
end function

' NAMED `read_file`, NOT `read`, and that is a CORRECTION to §7's spelling
' rather than a preference. `read` is a built-in: a library function of that
' name shadows it for every unqualified call INSIDE this library, so
' `finio.read` would call itself instead of reading the file, and `scan` could
' not read one at all. `discovery` made the same choice for the same reason and
' the rule there applies here -- a name that needs explaining at each call site
' is the wrong name.
function read_file(reg, path, options)
    f {file}= path
    return read_text(reg, read(f), _merge_origin(options, path))
end function

function _merge_origin(options, path)
    out = options
    if not has(out, "origin") then
        out.origin = path
    end if
    return out
end function

' --- §15: reading and validating are different operations -----------------
'
' "Invalid input is not necessarily unreadable input." A file whose control
' totals disagree with its entries is the single most operationally important
' thing an ACH shop can be told, and it is not a reason to refuse to read the
' file -- it is a reason to read it and REPORT. So `read` preserves and
' explains, `validate` judges, and the two are never the same call.

function validate(reg, doc)
    a = find_adapter(reg, doc.adapter)
    ' AN ADAPTER WITH NO VALIDATOR IS REFUSED, NOT ANSWERED WITH `[]`. An empty
    ' issue list means "this source conforms"; returning one for an adapter
    ' that never looked would report the strongest possible conformance claim
    ' on no evidence, and a caller could not tell it from a clean file. A
    ' caller who wants to know first can ask `has(adapter, "validate")`.
    if not has(a, "validate") then
        error ("finio.validate: adapter '" + doc.adapter + "' declares no validator, so it cannot say whether this source conforms -- an empty issue list would claim that it does")
    end if
    fn = a.validate
    issues = fn(doc)
    if type(issues) != "array" then
        error "finio.validate: adapter '" + doc.adapter + "' returned a " + type(issues) + ", not an array of issues"
    end if
    for each i in issues
        checked = _options(i, [ "code", "severity", "message", "record", "concept",
                                "expected", "found" ], "finio.validate issue")
        for each field in [ "code", "severity", "message" ]
            v = _required(i, field, "finio.validate issue")
        end for
        ' AXIOM 9: the framework grades CONFORMANCE, never consequence. A
        ' reader deciding whether a mismatch is worth stopping a payment run
        ' for is the consuming application's judgement, so the vocabulary has
        ' no "critical" in it -- only how the SOURCE stands against the format.
        if not contains([ "error", "warning", "note" ], i.severity) then
            error ("finio.validate: adapter '" + doc.adapter + "' used severity '" + string(i.severity) + "'; the format-conformance vocabulary is error, warning, note (Axiom 9 -- operational severity is the consumer's)")
        end if
    end for
    return issues
end function

' --- §16 + §17: writing ----------------------------------------------------
'
' DISPATCHED LIKE READING, so a consumer of the framework never has to name the
' adapter it happens to be holding -- which is the whole point of there being a
' framework. An adapter with no writer is REFUSED rather than answered with the
' source text: returning the bytes that arrived would be a perfect round trip
' that serialised nothing, and a caller could not tell it from an adapter that
' had written the document it was given.
'
' THE LOSS REPORT COMES BACK BESIDE THE TEXT, never instead of it (Axiom 8).
' Lossy output may be permitted; what may not happen is it being silent.

function write_text(reg, doc)
    a = find_adapter(reg, doc.adapter)
    if not has(a, "write") then
        error ("finio.write_text: adapter '" + doc.adapter + "' declares no writer")
    end if
    fn = a.write
    out = fn(doc)
    if not has(out, "text") then
        error ("finio.write_text: adapter '" + doc.adapter + "' returned no text")
    end if
    return { text: out.text,
             loss: _default(out, "loss", []),
             adapter: doc.adapter,
             revision: doc.revision,
             ' §17: an adapter states which guarantee it can provide, and the
             ' answer travels WITH the output rather than being looked up
             ' somewhere else by a caller who may not think to.
             byte_fidelity: a.byte_fidelity }
end function

function write_file(reg, doc, path)
    out = write_text(reg, doc)
    f {file}= path
    write(f, out.text)
    return out
end function

' --- Axiom 8: loss is explicit --------------------------------------------
'
' A LOSS NOTE IS A VALUE, so a transformation that cannot preserve everything
' has somewhere to say so that is not a print statement. The three kinds are
' the three ways a transformation actually loses: it did not claim the bytes,
' it could not hold the value at all, or it held less of it than arrived.

function loss_kinds()
    return [ "uninterpreted", "unrepresentable", "narrowed" ]
end function

function loss_note(kind, detail)
    if not contains(loss_kinds(), kind) then
        error ("finio.loss_note: '" + string(kind) + "' is not one of " + join(loss_kinds(), ", "))
    end if
    checked = _options(detail, [ "record", "concept", "byte_offset", "byte_length",
                                 "raw", "why" ], "finio.loss_note")
    w = _required(detail, "why", "finio.loss_note")
    return { kind: kind, record: _default(detail, "record", unknown),
             concept: _default(detail, "concept", unknown),
             byte_offset: _default(detail, "byte_offset", unknown),
             byte_length: _default(detail, "byte_length", unknown),
             raw: _default(detail, "raw", unknown),
             why: w }
end function

' --- §18: what the layout did NOT claim -----------------------------------
'
' The design's §18 requires that unknown fields be PRESERVED AS UNKNOWN rather
' than dropped, because local interpretation usually lives exactly there. For a
' fixed-width record that is arithmetic: the fields cover part of the record
' and the rest is a gap. `coverage` reports the gaps and the overlaps, and an
' adapter that quietly forgot a field shows up here as a gap rather than as
' nothing at all.

function coverage(lay, record_length)
    marks = []
    i = 0
    while i < record_length
        append(marks, 0)
        i = i + 1
    end while
    for each f in lay
        j = f.offset
        while j < f.offset + f.length
            if j < record_length then
                marks[j] = marks[j] + 1
            end if
            j = j + 1
        end while
    end for
    gaps = []
    overlaps = []
    run_start = unknown
    run_kind = -1
    i = 0
    while i <= record_length
        k = -1
        if i < record_length then
            k = marks[i]
            if k > 1 then
                k = 2
            end if
        end if
        if k != run_kind then
            if run_kind = 0 then
                append(gaps, { offset: run_start, length: i - run_start })
            end if
            if run_kind = 2 then
                append(overlaps, { offset: run_start, length: i - run_start })
            end if
            run_start = i
            run_kind = k
        end if
        i = i + 1
    end while
    return { gaps: gaps, overlaps: overlaps,
             complete: count(gaps) = 0 and count(overlaps) = 0 }
end function

' --- §8: the archive workflow ---------------------------------------------
'
' A report over a directory, and the counts it gives are the ones §8 asks for.
' UNREADABLE IS ITS OWN OUTCOME, separate from unrecognised: a file nothing
' claims and a file an adapter claimed and then could not read are different
' facts about an archive, and collapsing them hides which of the two a holder
' has to act on.

function scan(reg, directory)
    seen = {}
    files = []
    unknown_files = []
    ambiguous_files = []
    unreadable = []
    for each entry in list_files(directory)
        name = string(entry)
        append(files, name)
        ' A FILE WHOSE BYTES CANNOT BE READ IS ITS OWN OUTCOME. Letting it
        ' raise would end a scan over an archive because of one file, and
        ' counting it as unrecognised would say something false about the
        ' archive's contents -- an operator deciding what to do about 11
        ' unknown files needs to know which of them nobody could even open.
        on error goto next
        text = read(entry)
        if error then
            append(unreadable, { file: name, why: error.message })
            error.clear()
            on error stop
            continue
        end if
        on error stop
        ident = identify(reg, text)
        if ident.classification = "unknown" then
            append(unknown_files, name)
        else
            if ident.classification = "ambiguous" then
                append(ambiguous_files, { file: name, candidates: ident.candidates })
            else
                c = ident.candidates[0]
                key = c.adapter
                if not is_unknown(c.revision) then
                    key = key + "/" + string(c.revision)
                end if
                if has(seen, key) then
                    seen[key] = seen[key] + 1
                else
                    seen[key] = 1
                end if
            end if
        end if
    end for
    return { examined: count(files), files: files, counts: seen,
             unknown: unknown_files, ambiguous: ambiguous_files,
             unreadable: unreadable }
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
