' consolidate.bas — L3: many messy frames with the same meaning, one clean one.
'
' docs/xlsx_design.md §6. The participation-loan / CECL problem: a dozen credit
' unions send a dozen tapes that mean the same thing and look nothing alike.
' One calls it "Int Rate" and another "Rate (%)"; one writes a balance as a
' number and another as "$1,500.00" or "(1,200.00)"; one writes a rate as 5.25
' and another as 0.0475.
'
' THE RULE, inherited from ARI and for the same reason: INFER TO ADVISE, DECLARE
' TO PARSE. Anything that can be read two ways is either declared in the spec or
' reported — never quietly resolved. A tape whose rates are silently off by 100x
' still produces a plausible-looking pool.
'
' Provenance is not optional. Every output row carries the source it came from,
' because a consolidated figure nobody can trace back is not auditable.

library consolidate
    load frame from "frame.bas"

    ' ------------------------------------------------------------ name matching

    ' Normalised for comparison: case, whitespace and punctuation removed, so
    ' "Rate (%)", "rate %" and "RATE" all collapse together. This is the fuzz
    ' that saves an alias table from having to list every spelling; the alias
    ' list exists for the ones fuzz cannot reach ("Note ID" -> loan_id).
    function normalize_name(s)
        out = ""
        i = 0
        low = lower(string(s))
        while i < len(low)
            ch = mid(low, i, 1)
            keep = false
            if ch >= "a" and ch <= "z" then
                keep = true
            end if
            if ch >= "0" and ch <= "9" then
                keep = true
            end if
            if keep then
                out = out + ch
            end if
            i = i + 1
        end while
        return out
    end function

    ' Which source column feeds `canon`? The declared aliases are tried first,
    ' then the canonical name itself, both under normalisation.
    function _find_column(cols, canon, aliases)
        wanted = []
        for each a in aliases
            append(wanted, normalize_name(a))
        end for
        append(wanted, normalize_name(canon))
        for each w in wanted
            for each c in cols
                if normalize_name(c) = w then
                    return c
                end if
            end for
        end for
        return unknown
    end function

    ' ------------------------------------------------------------ normalizers

    ' Money in the forms a report actually uses: a bare number, "$1,500.00",
    ' "(1,200.00)" for a negative, a trailing minus, and thousands separators.
    ' Deliberately the same union ari.bas recognises in print-image reports —
    ' the surface chaos is identical, only the substrate differs.
    function to_money(v)
        if is_unknown(v) then
            return unknown
        end if
        if is_number(v) then
            return v
        end if
        s = trim(string(v))
        if s = "" then
            return unknown
        end if
        neg = false
        if starts_with(s, "(") and ends_with(s, ")") then
            neg = true
            s = mid(s, 1, len(s) - 2)
        end if
        if ends_with(s, "-") then
            neg = true
            s = mid(s, 0, len(s) - 1)
        end if
        if starts_with(s, "-") then
            neg = true
            s = mid(s, 1, len(s) - 1)
        end if
        s = replace(s, regex("[$,\\s]"), "")
        if s = "" then
            return unknown
        end if
        if not contains(s, regex("^[0-9]+(\\.[0-9]+)?$")) then
            return unknown
        end if
        n = number(s)
        if neg then
            return 0 - n
        end if
        return n
    end function

    ' A percent, returned as a FRACTION (0.0525 for 5.25%).
    '
    ' A string carrying "%" is unambiguous and is simply divided by 100. A bare
    ' NUMBER is not: 5.25 and 0.0525 are both plausible ways to write the same
    ' rate, and nothing in the file distinguishes them. `scale` therefore takes
    ' "whole", "fraction", or "infer" — and inference is a per-COLUMN judgement
    ' made from every value at once (see infer_percent_scale), never per cell,
    ' because a cell in isolation cannot be judged at all.
    function to_percent(v, scale)
        if is_unknown(v) then
            return unknown
        end if
        if is_string(v) then
            s = trim(string(v))
            had = contains(s, "%")
            s = replace(s, regex("[%\\s,]"), "")
            if s = "" then
                return unknown
            end if
            if not contains(s, regex("^-?[0-9]+(\\.[0-9]+)?$")) then
                return unknown
            end if
            n = number(s)
            if had then
                return n / 100.0
            end if
            return _apply_scale(n, scale)
        end if
        if is_number(v) then
            return _apply_scale(v, scale)
        end if
        return unknown
    end function

    function _apply_scale(n, scale)
        if scale = "whole" then
            return n / 100.0
        end if
        return n
    end function

    ' Judge a whole column at once. Every rate in a tape is written the same
    ' way, so the column is decidable even though a single cell is not: if any
    ' value exceeds 1, the column must be whole percents, since a fraction
    ' above 1 would be a rate over 100%.
    '
    ' The ambiguous case is real and is REPORTED rather than guessed: a column
    ' whose values all sit at or below 1 could be fractions, or whole percents
    ' that happen to be under 1% — and for a pool of loans that difference is
    ' 100x.
    function infer_percent_scale(values)
        seen = false
        any_over_1 = false
        any_sign = false
        for each v in values
            n = unknown
            if is_number(v) then
                n = v
            end if
            if is_string(v) then
                if contains(string(v), "%") then
                    any_sign = true
                end if
                s = replace(string(v), regex("[%\\s,]"), "")
                if contains(s, regex("^-?[0-9]+(\\.[0-9]+)?$")) then
                    n = number(s)
                end if
            end if
            if not is_unknown(n) then
                seen = true
                if abs(n) > 1 then
                    any_over_1 = true
                end if
            end if
        end for
        if not seen then
            return { scale: "fraction", certain: false, why: "no numeric values to judge" }
        end if
        ' A written "%" settles it outright -- there is nothing to infer, and
        ' saying "inferred" would overstate how much guessing happened.
        if any_sign then
            return { scale: "whole", certain: true,
                     why: "the values carry a written % sign" }
        end if
        if any_over_1 then
            return { scale: "whole", certain: true,
                     why: "a value exceeds 1, which no fraction rate can" }
        end if
        return { scale: "fraction", certain: false,
                 why: "every value is at or below 1: fractions, or whole percents under 1% -- declare `scale` to be sure" }
    end function

    function to_text(v)
        if is_unknown(v) then
            return unknown
        end if
        return trim(string(v))
    end function

    function to_number(v)
        if is_unknown(v) then
            return unknown
        end if
        if is_number(v) then
            return v
        end if
        s = replace(trim(string(v)), regex("[,\\s]"), "")
        if not contains(s, regex("^-?[0-9]+(\\.[0-9]+)?$")) then
            return unknown
        end if
        return number(s)
    end function

    ' ------------------------------------------------------------------ merge

    ' merge(sources, spec) -> { ok, frame, report }
    '
    ' `sources` is a list of { name: "CU_A", frame: f }.
    ' `spec.columns` maps a canonical name to { names: [aliases], kind: type,
    ' required: bool, scale: "whole"|"fraction"|"infer" }.
    '
    ' The fields are `names` and `kind` rather than the more natural `from` and
    ' `as` because both of those are RESERVED WORDS in gBASIC (`load X from`,
    ' and ARI's `as money`), and a reserved word cannot be a record key.
    ' `spec.source_column` names the provenance column (default "source").
    '
    ' A source missing a REQUIRED column is REJECTED, not emitted with unknowns:
    ' a tape silently short a balance column understates the pool, and an
    ' understated pool looks exactly like a small one.
    function merge(sources, spec)
        src_col = "source"
        if has(spec, "source_column") then
            src_col = spec.source_column
        end if
        canon_names = keys(spec.columns)

        rows = []
        notes = []
        accepted = []
        rejected = []

        for each s in sources
            cols = frame.columns(s.frame)
            mapping = { }
            missing = []
            for each canon in canon_names
                rule = spec.columns[canon]
                aliases = []
                if has(rule, "names") then
                    aliases = rule.names
                end if
                found = _find_column(cols, canon, aliases)
                if is_unknown(found) then
                    req = false
                    if has(rule, "required") then
                        req = rule.required
                    end if
                    if req then
                        append(missing, canon)
                    end if
                else
                    mapping[canon] = found
                end if
            end for

            if count(missing) > 0 then
                append(rejected, s.name)
                append(notes, s.name + ": REJECTED, missing required column(s) " + join(missing, ", "))
                continue
            end if
            append(accepted, s.name)

            ' Percent scale is decided per source and per column, from all of
            ' that column's values, then reported.
            scales = { }
            for each canon in canon_names
                rule = spec.columns[canon]
                if not has(mapping, canon) then
                    continue
                end if
                if rule.kind != "percent" then
                    continue
                end if
                declared = "infer"
                if has(rule, "scale") then
                    declared = rule.scale
                end if
                if declared != "infer" then
                    scales[canon] = declared
                    append(notes, s.name + "." + canon + ": scale declared as " + declared)
                else
                    v = s.frame[mapping[canon]]
                    guess = infer_percent_scale(v)
                    scales[canon] = guess.scale
                    if guess.certain then
                        append(notes, s.name + "." + canon + ": inferred " + guess.scale + " (" + guess.why + ")")
                    else
                        append(notes, s.name + "." + canon + ": AMBIGUOUS, assumed " + guess.scale + " (" + guess.why + ")")
                    end if
                end if
            end for

            for each row in frame.to_rows(s.frame)
                rec = { }
                rec[src_col] = s.name
                for each canon in canon_names
                    if not has(mapping, canon) then
                        rec[canon] = unknown
                        continue
                    end if
                    raw = row[mapping[canon]]
                    rule = spec.columns[canon]
                    if rule.kind = "money" then
                        rec[canon] = to_money(raw)
                    else
                        if rule.kind = "percent" then
                            sc = "fraction"
                            if has(scales, canon) then
                                sc = scales[canon]
                            end if
                            rec[canon] = to_percent(raw, sc)
                        else
                            if rule.kind = "number" then
                                rec[canon] = to_number(raw)
                            else
                                rec[canon] = to_text(raw)
                            end if
                        end if
                    end if
                end for
                append(rows, rec)
            end for
        end for

        return { ok: count(rejected) = 0,
                 frame: frame.from_rows(rows),
                 accepted: accepted,
                 rejected: rejected,
                 notes: notes }
    end function

end library
