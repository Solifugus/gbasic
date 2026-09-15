' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' finio_watch -- the MONITORING MECHANISM (docs/financial_adapters_design.md
' §13, "The monitoring mechanism", and §9's ObservationLog).
'
' §13 said what to RECORD and what to do ONCE A REVISION IS DISCOVERED, and
' never said what a watch source is, what checking one does, what triggers a
' check or what it emits. Without those, "continually maintained" is an
' intention. This is the mechanism.
'
' IT DETECTS. IT NEVER UPDATES. That is the load-bearing decision and it falls
' out of Axiom 4 and step 7 of §13's procedure ("preserve the old revision
' unchanged"): a process that modified an adapter would silently change how a
' file written in 2019 is read, which is the one thing this framework exists to
' prevent. So a check produces WORK -- an entry in a review queue naming what
' moved and what says so -- and never a patch. There is deliberately no
' function in this library that returns a modified adapter.
'
' AND IT PERFORMS NO I/O. `check` is a pure function of what the CALLER
' fetched. That is the shape `agent.apply` and `nlq`'s three steps already use
' here, and it is what makes a maintenance process testable with no network --
' which matters, because a gate that needs the internet goes red when somebody
' else's site is down and then gets turned off. The application decides whether
' to fetch on a timer, on a worker, or once a quarter by hand.

library finio_watch

load finio from "finio.bas"

' --- what a watch source is ------------------------------------------------

function source_kinds()
    return [ "version_catalogue", "document", "changelog", "registry_page" ]
end function

' `watching` IS REQUIRED AND IS NOT DECORATION. A catalogue page changes when
' its footer year changes, and that is not evidence a message definition moved.
' Recording what a change would MEAN is what separates a signal from a diff --
' and it is the sentence a person reads first when the finding lands.
function source(kind, detail)
    if not contains(source_kinds(), kind) then
        error ("finio_watch.source: '" + string(kind) + "' is not one of " + join(source_kinds(), ", "))
    end if
    checked = _options(detail, [ "reference", "watching", "last_seen", "last_checked" ],
                       "finio_watch.source(" + kind + ")")
    r = _required(detail, "reference", "finio_watch.source")
    w = _required(detail, "watching", "finio_watch.source")
    out = { kind: kind, reference: r, watching: w }
    if has(detail, "last_seen") then
        out.last_seen = detail.last_seen
    end if
    if has(detail, "last_checked") then
        out.last_checked = detail.last_checked
    end if
    return out
end function

function finding_kinds()
    return [ "first_sight", "unchanged", "changed", "unreachable" ]
end function

' `observed` is what the caller's fetch produced:
'   { ok: true, content: "..." }   or   { ok: false, why: "HTTP 403" }
'
' UNREACHABLE IS ITS OWN OUTCOME AND NOT A QUIET `unchanged`. A source that has
' 403'd for six months is not a stable format, it is a watch that stopped
' working, and reporting it as "no change" is how a monitoring process comes to
' assert the world is still by observing nothing. This is not hypothetical:
' Nacha's own developer guide returned 403 to an automated fetch during the
' first survey, and an `unchanged` there would have been a lie about ACH.
function check(src, observed, today)
    checked = _options(observed, [ "ok", "content", "why" ], "finio_watch.check observed")
    ok = _required(observed, "ok", "finio_watch.check observed")
    if ok != true then
        return { finding: "unreachable",
                 reference: src.reference,
                 watching: src.watching,
                 checked: today,
                 why: _default(observed, "why", "the caller did not say why"),
                 means: ("this watch is not working; it says nothing about whether "
                         + string(src.reference) + " has moved") }
    end if
    seen = _fingerprint(_required(observed, "content", "finio_watch.check observed"))
    if not has(src, "last_seen") then
        return { finding: "first_sight", reference: src.reference, watching: src.watching,
                 checked: today, fingerprint: seen,
                 means: "nothing was known about this source before; this is the baseline" }
    end if
    if src.last_seen = seen then
        return { finding: "unchanged", reference: src.reference, watching: src.watching,
                 checked: today, fingerprint: seen, means: "" }
    end if
    return { finding: "changed", reference: src.reference, watching: src.watching,
             checked: today, fingerprint: seen, was: src.last_seen,
             means: src.watching }
end function

' A CONTENT FINGERPRINT, NOT THE CONTENT. What is retained between checks is a
' short digest: keeping the page would make the registry a cache of other
' people's documents, which is a redistribution question (every entry here says
' spec_redistribution_allowed: false) as well as a storage one. 32-bit FNV-1a,
' written here for the same reason `llm`'s replay key is -- `crypto` needs
' libcrypto and a maintenance process must not acquire a native dependency to
' notice that a page changed. A collision would report `unchanged` for a source
' that moved, so the LENGTH is carried beside it: two documents colliding AND
' being the same length is the case this trades away, deliberately and visibly.
function _fingerprint(text)
    h = 2166136261
    n = byte_count(text)
    i = 0
    while i < n
        h = h - floor(h / 4294967296) * 4294967296
        h = h + 0
        b = byte_at(text, i)
        h = _xor32(h, b)
        h = h * 16777619
        h = h - floor(h / 4294967296) * 4294967296
        i = i + 1
    end while
    return string(n) + ":" + hex_encode(from_bytes([ floor(h / 16777216) - floor(h / 4294967296) * 256,
                                                     floor(h / 65536) - floor(h / 16777216) * 256,
                                                     floor(h / 256) - floor(h / 65536) * 256,
                                                     h - floor(h / 256) * 256 ]))
end function

function _xor32(a, b)
    out = 0
    bit = 1
    i = 0
    while i < 32
        ab = floor(a / bit) - floor(a / (bit * 2)) * 2
        bb = floor(b / bit) - floor(b / (bit * 2)) * 2
        if ab != bb then
            out = out + bit
        end if
        bit = bit * 2
        i = i + 1
    end while
    return out
end function

' --- §9's ObservationLog: what production has seen -------------------------
'
' Top-down watching finds a revision when a standards body publishes it.
' BOTTOM-UP OBSERVATION finds one when files start arriving the adapter cannot
' fully explain, which is usually sooner and always more specific. The adapters
' already produce the raw material -- every `loss_note("uninterpreted", ...)`
' is a token preserved and unaccounted for -- and what was missing is that
' NOTHING COUNTED THEM. §9: "Preserving an unknown value is only half the
' benefit; counting it is what turns it into work."

function observation_kinds()
    return [ "unknown_code", "unknown_field", "unexpected_length",
             "unparsed_region", "contradictory_context" ]
end function

' A TOKEN AND A LOCATION, NEVER CONTENT, AND IT IS ENFORCED RATHER THAN ASKED
' FOR. §9: observations "record tokens and locations, never account numbers,
' party names, amounts, or any other content of a customer record". The
' permitted fields carry none of those, and a `detail` longer than a token is
' REFUSED -- because a whole record passed as a "token" is a customer record in
' a log, and that is the one mistake this log could make that would matter.
function max_detail_bytes()
    return 64
end function

function observation(obs)
    checked = _options(obs, [ "adapter", "revision", "kind", "detail", "where", "seen" ],
                       "finio_watch.observation")
    for each f in [ "adapter", "revision", "kind", "detail", "seen" ]
        v = _required(obs, f, "finio_watch.observation")
    end for
    if not contains(observation_kinds(), obs.kind) then
        error ("finio_watch.observation: '" + string(obs.kind) + "' is not one of " + join(observation_kinds(), ", "))
    end if
    if byte_count(string(obs.detail)) > max_detail_bytes() then
        error ("finio_watch.observation: the detail is " + string(byte_count(string(obs.detail)))
               + " bytes and an observation records a TOKEN, not content (limit "
               + string(max_detail_bytes()) + "). §9: observations record tokens and locations, never the content of a customer record.")
    end if
    return obs
end function

' COUNTING IS THE WHOLE POINT. One unknown service class code is a curiosity;
' three hundred of them across last quarter's files is a stronger signal that a
' revision happened than any watch list, and it names the field to go and read
' about.
function observe(log, obs)
    o = observation(obs)
    key = string(o.adapter) + "/" + string(o.revision) + "/" + string(o.kind) + "/" + string(o.detail)
    out = log
    if has(out, key) then
        e = out[key]
        e.occurrences = e.occurrences + 1
        if o.seen > e.last_seen then
            e.last_seen = o.seen
        end if
        if o.seen < e.first_seen then
            e.first_seen = o.seen
        end if
        ' A BOUNDED SAMPLE OF WHERE (§9), not every location. An unbounded list
        ' grows with the traffic rather than with the finding, and the tenth
        ' example teaches nothing the third did not.
        if has(o, "where") and count(e.where) < 5 then
            if not contains(e.where, o.where) then
                append(e.where, o.where)
            end if
        end if
        out[key] = e
    else
        w = []
        if has(o, "where") then
            append(w, o.where)
        end if
        out[key] = { adapter: o.adapter, revision: o.revision, kind: o.kind,
                     detail: o.detail, first_seen: o.seen, last_seen: o.seen,
                     occurrences: 1, where: w }
    end if
    return out
end function

function observations_for(log, adapter)
    out = []
    for each k in keys(log)
        if log[k].adapter = adapter then
            append(out, log[k])
        end if
    end for
    return out
end function

' --- what is due, and what is worth reading about --------------------------
'
' §13: "Staleness should be derivable, not remembered."

function due(entries, today)
    out = []
    for each e in entries
        pri = "periodic"
        if has(e, "maintenance_priority") then
            pri = e.maintenance_priority
        end if
        ' A DORMANT FORMAT IS NOT OVERDUE. §13: "A historical format whose final
        ' revision is decades old may require little or no routine monitoring."
        ' Without this the queue fills with formats nobody expects to move and
        ' the ones that do move are buried -- which is how a maintenance list
        ' stops being read.
        if pri != "dormant" then
            if has(e, "next_review_due") then
                if string(e.next_review_due) <= string(today) then
                    append(out, { id: e.id, due: e.next_review_due, priority: pri })
                end if
            else
                append(out, { id: e.id, due: unknown, priority: pri,
                              why: "no next_review_due has ever been set" })
            end if
        end if
    end for
    return out
end function

' THE SIGNAL IS THE TWO TOGETHER, which §13 already argued and nothing
' implemented: "An adapter with old evidence and no observations may be
' perfectly healthy -- a stable format simply is not moving. An adapter with
' recent evidence and a rising observation count is the interesting case, and
' only the two together say so."
function review_queue(entries, log, today)
    out = []
    for each d in due(entries, today)
        obs = observations_for(log, d.id)
        total = 0
        for each o in obs
            total = total + o.occurrences
        end for
        reason = "review is due"
        rank = 1
        if total > 0 then
            reason = ("review is due, and " + string(count(obs)) + " kind(s) of thing this adapter could not explain have been seen "
                      + string(total) + " time(s)")
            rank = 2 + count(obs)
        end if
        append(out, { id: d.id, due: d.due, priority: d.priority,
                      observation_kinds: count(obs), occurrences: total,
                      rank: rank, why: reason })
    end for
    ' Observations on an adapter that is NOT due still matter -- that is the
    ' case §13 calls "the interesting one", and a queue keyed only on the
    ' calendar would never show it.
    for each k in keys(log)
        o = log[k]
        already = false
        for each q in out
            if q.id = o.adapter then
                already = true
            end if
        end for
        if not already then
            append(out, { id: o.adapter, due: unknown, priority: "unscheduled",
                          observation_kinds: 1, occurrences: o.occurrences,
                          rank: 2 + o.occurrences,
                          why: ("not due, but production has produced " + string(o.occurrences)
                                + " thing(s) this adapter could not explain -- which is the case §13 calls interesting") })
        end if
    end for
    return _rank_desc(out)
end function

' Selection sort over an index, not by removing from the array. gBASIC has no
' remove-at-index builtin, and rebuilding the array each pass would be the
' quadratic-copy trap UNLEARN warns about for a list that could hold every
' format in the registry.
function _rank_desc(items)
    n = count(items)
    taken = []
    i = 0
    while i < n
        append(taken, false)
        i = i + 1
    end while
    out = []
    k = 0
    while k < n
        best = -1
        i = 0
        while i < n
            if not taken[i] then
                if best < 0 or items[i].rank > items[best].rank then
                    best = i
                end if
            end if
            i = i + 1
        end while
        taken[best] = true
        append(out, items[best])
        k = k + 1
    end while
    return out
end function

' --- helpers ---------------------------------------------------------------

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
