' studio_json.bas — a non-raising JSON validity gate for gBASIC Studio.
'
' General-purpose (not Studio-specific in mechanism): the core `decode` builtin
' RAISES on malformed JSON, and gBASIC's `on error resume next` cannot catch a
' raise and return a clean fallback to its caller (see docs/ai/UNLEARN.md). So a
' program that must tolerate a corrupt/truncated file cannot simply `decode` and
' recover — it has to PRE-VALIDATE. This library is that gate: a recursive-descent
' JSON *validator* that returns a boolean (never raises), so `decode` is only ever
' reached on input already known to be well-formed. It mirrors the pattern
' stdlib/llm.bas already uses internally for model output.
'
' Scope: RFC-8259 structure (object/array/string/number/true/false/null with
' escapes and unicode \uXXXX). It validates syntax, not schema.
library studio_json

    ' Single codepoint at index i (0-based), or "" past the end. Strings are not
    ' indexable in gBASIC, so character access goes through mid (0-based).
    function _ch(s, i, n)
        if i >= n then
            return ""
        end if
        return mid(s, i, 1)
    end function

    ' Advance past JSON whitespace, returning the next index.
    function _skip_ws(s, i, n)
        j = i
        while j < n
            c = studio_json._ch(s, j, n)
            if c = " " or c = chr(9) or c = chr(10) or c = chr(13) then
                j = j + 1
            else
                break
            end if
        end while
        return j
    end function

    function _is_digit(c)
        if c = "" then
            return false
        end if
        ge = c >= "0"
        le = c <= "9"
        return ge and le
    end function

    function _is_hex(c)
        if studio_json._is_digit(c) then
            return true
        end if
        lc = lower(c)
        gea = lc >= "a"
        lef = lc <= "f"
        return gea and lef
    end function

    ' Scan a JSON string starting at the opening quote (s[i] = '"').
    ' Return the index just past the closing quote, or -1 on malformed.
    function _scan_string(s, i, n)
        opener = studio_json._ch(s, i, n)
        if opener != chr(34) then
            return -1
        end if
        j = i + 1
        while j < n
            c = studio_json._ch(s, j, n)
            if c = chr(34) then
                return j + 1
            end if
            if c = "\\" then
                j = j + 1
                e = studio_json._ch(s, j, n)
                if e = "" then
                    return -1
                end if
                if e = chr(34) or e = "\\" or e = "/" or e = "b" or e = "f" or e = "n" or e = "r" or e = "t" then
                    j = j + 1
                else
                    if e = "u" then
                        k = 0
                        while k < 4
                            h = studio_json._ch(s, j + 1 + k, n)
                            if not studio_json._is_hex(h) then
                                return -1
                            end if
                            k = k + 1
                        end while
                        j = j + 5
                    else
                        return -1
                    end if
                end if
            else
                j = j + 1
            end if
        end while
        return -1
    end function

    ' Scan a JSON number. Return the index past it, or -1 on malformed.
    function _scan_number(s, i, n)
        j = i
        c = studio_json._ch(s, j, n)
        if c = "-" then
            j = j + 1
        end if
        c = studio_json._ch(s, j, n)
        if c = "0" then
            j = j + 1
        else
            if studio_json._is_digit(c) then
                while studio_json._is_digit(studio_json._ch(s, j, n))
                    j = j + 1
                end while
            else
                return -1
            end if
        end if
        ' fraction
        if studio_json._ch(s, j, n) = "." then
            j = j + 1
            if not studio_json._is_digit(studio_json._ch(s, j, n)) then
                return -1
            end if
            while studio_json._is_digit(studio_json._ch(s, j, n))
                j = j + 1
            end while
        end if
        ' exponent
        e = studio_json._ch(s, j, n)
        if e = "e" or e = "E" then
            j = j + 1
            sgn = studio_json._ch(s, j, n)
            if sgn = "+" or sgn = "-" then
                j = j + 1
            end if
            if not studio_json._is_digit(studio_json._ch(s, j, n)) then
                return -1
            end if
            while studio_json._is_digit(studio_json._ch(s, j, n))
                j = j + 1
            end while
        end if
        return j
    end function

    ' Match a bare literal word (true/false/null) at i. Return index past it or -1.
    function _scan_word(s, i, n, word)
        wl = len(word)
        seg = mid(s, i, wl)
        if seg = word then
            return i + wl
        end if
        return -1
    end function

    ' Scan any JSON value at i (after optional leading ws is the caller's job for
    ' the top level; internal calls skip ws themselves). Return index past it,
    ' or -1 on malformed.
    function _scan_value(s, i, n)
        j = studio_json._skip_ws(s, i, n)
        c = studio_json._ch(s, j, n)
        if c = "" then
            return -1
        end if
        if c = "{" then
            return studio_json._scan_object(s, j, n)
        end if
        if c = "[" then
            return studio_json._scan_array(s, j, n)
        end if
        if c = chr(34) then
            return studio_json._scan_string(s, j, n)
        end if
        if c = "t" then
            return studio_json._scan_word(s, j, n, "true")
        end if
        if c = "f" then
            return studio_json._scan_word(s, j, n, "false")
        end if
        if c = "n" then
            return studio_json._scan_word(s, j, n, "null")
        end if
        return studio_json._scan_number(s, j, n)
    end function

    function _scan_object(s, i, n)
        j = i + 1
        j = studio_json._skip_ws(s, j, n)
        if studio_json._ch(s, j, n) = "}" then
            return j + 1
        end if
        while true
            j = studio_json._skip_ws(s, j, n)
            if studio_json._ch(s, j, n) != chr(34) then
                return -1
            end if
            j = studio_json._scan_string(s, j, n)
            if j < 0 then
                return -1
            end if
            j = studio_json._skip_ws(s, j, n)
            if studio_json._ch(s, j, n) != ":" then
                return -1
            end if
            j = studio_json._scan_value(s, j + 1, n)
            if j < 0 then
                return -1
            end if
            j = studio_json._skip_ws(s, j, n)
            sep = studio_json._ch(s, j, n)
            if sep = "," then
                j = j + 1
            else
                if sep = "}" then
                    return j + 1
                end if
                return -1
            end if
        end while
        return -1
    end function

    function _scan_array(s, i, n)
        j = i + 1
        j = studio_json._skip_ws(s, j, n)
        if studio_json._ch(s, j, n) = "]" then
            return j + 1
        end if
        while true
            j = studio_json._scan_value(s, j, n)
            if j < 0 then
                return -1
            end if
            j = studio_json._skip_ws(s, j, n)
            sep = studio_json._ch(s, j, n)
            if sep = "," then
                j = j + 1
            else
                if sep = "]" then
                    return j + 1
                end if
                return -1
            end if
        end while
        return -1
    end function

    ' ---- public API --------------------------------------------------------

    ' True when `text` is well-formed JSON (a single value plus optional
    ' surrounding whitespace, nothing trailing). Never raises.
    function valid(text)
        n = len(text)
        j = studio_json._scan_value(text, 0, n)
        if j < 0 then
            return false
        end if
        j = studio_json._skip_ws(text, j, n)
        return j = n
    end function

    ' Decode `text` if it is valid JSON, otherwise return `fallback`. `decode` is
    ' only reached on validated input, so this never raises on malformed input.
    function parse_or(text, fallback)
        ok = studio_json.valid(text)
        if not ok then
            return fallback
        end if
        return decode(text)
    end function

end library
