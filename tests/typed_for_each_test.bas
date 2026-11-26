' TYPED `for each` ELEMENTS (docs/typed_variables_design.md rule 6, Matthew's
' ruling 2026-11-25). Self-checking; run by tests/run_typed_locks.sh.
'
' `for each x:number in xs` binds each element as `x:number = element` binds
' it: converted, absence kept, refused at the `for each` naming the element's
' place. The lock outlives the loop, as every lock does. Each check sits beside
' the same loop without the type, so a type that did nothing shows.

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if type(got) = type(want) and string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + " (" + type(got) + "), want " + string(want) + " (" + type(want) + ")"
    end if
    return nothing
end function

' What a body raised (the declaration's line taken out), or ANSWERED.
function refusal(f)
    on error goto handle
    r = f()
    return "ANSWERED " + string(r)
handle:
    m = error.message
    error.clear()
    return replace(m, regex(" at line [0-9]+,"), ",")
end function

print "-- EACH ELEMENT CONVERTS"
function kinds_typed()
    out = ""
    for each x:number in [1, "2", "0.5"]
        out = out + type(x) + " "
    end for
    return out
end function
function kinds_plain()
    out = ""
    for each x in [1, "2", "0.5"]
        out = out + type(x) + " "
    end for
    return out
end function
check("x:number makes each element a number", kinds_typed(), "number number number ")
check("  control: untyped, the text stays text", kinds_plain(), "number string string ")

function sum_typed()
    s = 0
    for each x:number in ["4", 5, "6"]
        s = s + x
    end for
    return s
end function
check("so arithmetic on them adds", sum_typed(), 15)

print "-- ABSENCE IS KEPT, AS THE LOCK KEEPS IT"
function absent()
    out = ""
    for each x:number in [unknown, nothing, 3]
        out = out + string(x) + " "
    end for
    return out
end function
check("unknown and nothing pass through a number lock", absent(), "unknown nothing 3 ")

print "-- A VALUE THAT DOES NOT CONVERT STOPS THE LOOP, BY ITS PLACE"
function stops()
    seen = ""
    for each x:number in [1, "three", 3]
        seen = seen + string(x) + " "
    end for
    return seen
end function
check("the second element refused, naming it", refusal(stops),
      "x is locked to number by x:number, and element 2 does not convert: number modifier expects a valid number string")
function stops_seen()
    on error goto next
    seen = ""
    for each x:number in [1, "three", 3]
        seen = seen + string(x) + " "
    end for
    if error then
        seen = seen + "| refused"
    end if
    return seen + " | after"
end function
check("  the first ran, the third did not, and the function went on", stops_seen(), "1 | refused | after")
function stops_line()
    on error goto next
    for each x:number in [1, "three"]
        y = x
    end for
    if error then
        return error.line
    end if
    return 0
end function
check("  raised at the for each line", stops_line(), 94)

print "-- THE LOCK OUTLIVES THE LOOP"
function outlives()
    on error goto next
    for each x:number in [1]
        y = x
    end for
    x = "5"
    first = type(x) + " " + string(x)
    x = "five"
    if error then
        return first + " | " + replace(error.message, regex(" at line [0-9]+,"), ",")
    end if
    return first + " | not refused"
end function
check("an assignment after the loop converts and refuses", outlives(),
      "number 5 | x is locked to number by x:number, and this value does not convert: number modifier expects a valid number string")
function bare_after()
    for each x:number in [1]
        y = x
    end for
    for each x in [2]
        y = x
    end for
    return "ran"
end function
check("a later loop over the name must name the type", refusal(bare_after),
      "x is locked to number by x:number, so the loop must name the type: write `for each x:number in ...`, or use another name for the loop")
function same_again()
    s = 0
    for each x:number in [1]
        s = s + x
    end for
    for each x:number in ["2"]
        s = s + x
    end for
    return s
end function
check("  and may, with the type", same_again(), 3)
function relock()
    for each x:number in [1]
        y = x
    end for
    for each x:text in [2]
        y = x
    end for
    x = 7
    return type(x)
end function
check("  naming another type relocks, as x:text = e does", relock(), "string")
function empty_locks_nothing()
    for each x:number in []
        y = x
    end for
    x = "free"
    return x
end function
check("an empty loop binds nothing and locks nothing", empty_locks_nothing(), "free")

print "-- THE INDEX, AND OTHER TYPES AND SOURCES"
function with_index()
    s = 0
    for each x:number, i in ["10", "20"]
        s = s + x + i
    end for
    return s
end function
check("x:number, i: the element converts, the index counts", with_index(), 31)
function money_loop()
    out = ""
    for each m:money(USD) in ["2.50", 3]
        out = out + type(m) + " "
    end for
    return out
end function
check("money(USD) converts each element", money_loop(), "money money ")
function file_lines()
    p = temp_file()
    write(p, "1" + chr(10) + "2" + chr(10) + "39" + chr(10))
    s = 0
    for each n:number in {file}p
        s = s + n
    end for
    delete(p)
    return s
end function
check("a file's lines read as numbers", file_lines(), 42)
function file_lines_plain()
    p = temp_file()
    write(p, "1" + chr(10) + "2" + chr(10))
    out = ""
    for each n in {file}p
        out = out + type(n) + " "
    end for
    delete(p)
    return out
end function
check("  control: untyped, a line is text", file_lines_plain(), "string string ")

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
