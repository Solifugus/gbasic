' `else if` -- a condition chain closed by ONE `end if`.
'
' Added 2026-08-27. Until then gBASIC had no `else if` and nothing said so:
' docs/reference.md used one in its path-containment security example, excused
' by a `fragment` marker, and it would not have parsed. The alternative the
' language already had -- `consider true` with an `if` per branch -- appeared
' in ZERO files anywhere in the tree, which is why fifty sites in stdlib were
' written as nested staircases instead.
'
' It DESUGARS to the nested form rather than adding an AST node, so `--ast`
' shows the nesting that is really there and the evaluator is untouched. The
' tail RECURSES rather than nesting a whole if_statement, which is what makes
' one `end if` close the whole chain instead of one per rung.
'
' Measured: 0 new grammar conflicts, block and inline forms both.

results = []

function check(label, expected, actual)
    if expected = actual then
        print "ok   " + label
        return true
    end if
    print "MISMATCH " + label + ": expected '" + string(expected) + "', got '" + string(actual) + "'"
    return false
end function

' The real shape this exists for: stdlib/crypto.bas escape decoding, which was
' written as a three-deep staircase.
function classify(esc)
    if esc = "n" then
        return "newline"
    else if esc = "t" then
        return "tab"
    else if esc = "r" then
        return "return"
    else
        return "literal"
    end if
end function

append(results, check("first rung", "newline", classify("n")))
append(results, check("middle rung", "tab", classify("t")))
append(results, check("last rung", "return", classify("r")))
append(results, check("final else", "literal", classify("q")))

' A chain with NO trailing else falls through to nothing.
function no_else(n)
    out = "none"
    if n = 1 then
        out = "one"
    else if n = 2 then
        out = "two"
    end if
    return out
end function

append(results, check("no trailing else, matched", "two", no_else(2)))
append(results, check("no trailing else, unmatched", "none", no_else(9)))

' Inline consequents, inline rungs, and a block rung after an inline start.
function inline_chain(n)
    if n = 1 then return "one"
    else if n = 2 then return "two"
    else return "many"
end function

append(results, check("inline chain 1", "one", inline_chain(1)))
append(results, check("inline chain 2", "two", inline_chain(2)))
append(results, check("inline chain fallback", "many", inline_chain(5)))

function mixed(n)
    if n = 1 then return "one"
    else if n = 2 then
        return "two"
    end if
    return "other"
end function

append(results, check("inline start, block rung", "two", mixed(2)))
append(results, check("inline start, falls through", "other", mixed(3)))

' THE OLD NESTED FORM IS UNCHANGED. Fifty sites in stdlib are written this way
' and none of them were touched; `else if` is an addition, not a migration.
function nested(n)
    if n = 1 then
        return "one"
    else
        if n = 2 then
            return "two"
        else
            return "many"
        end if
    end if
end function

append(results, check("nested form still works", "two", nested(2)))
append(results, check("nested form fallback", "many", nested(7)))

' `consider` remains the better tool when the branches test ONE subject: it
' names the subject once. `else if` repeats it per rung.
function considered(esc)
    consider esc
    if "n" then
        return "newline"
    if "t" then
        return "tab"
    else
        return "literal"
    end consider
end function

append(results, check("consider on a value", "tab", considered("t")))
append(results, check("consider fallback", "literal", considered("z")))

' Chains nest inside each other without ambiguity.
function grid(a, b)
    if a = 1 then
        if b = 1 then
            return "11"
        else if b = 2 then
            return "12"
        end if
        return "1x"
    else if a = 2 then
        return "2x"
    end if
    return "xx"
end function

append(results, check("nested chain inner", "12", grid(1, 2)))
append(results, check("nested chain inner fallthrough", "1x", grid(1, 9)))
append(results, check("nested chain outer", "2x", grid(2, 1)))

bad = 0
for each v in results
    if not v then
        bad += 1
    end if
next v

print ""
print "checks: " + string(count(results))
print "mismatches: " + string(bad)
