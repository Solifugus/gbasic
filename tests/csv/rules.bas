' The reading rules of docs/csv_design.md §3-§5, one case each. Every case
' prints each record with where it started, or what was raised and its details.
CR = chr(13)

function show(label, text, opts)
    on error goto next
    print("-- " + label)
    for each r, at in csv.parse(text, opts)
        line = "  line " + string(at.line) + " @" + string(at.offset) + " #" + string(at.record) + " " + string(r)
        if not at.ok then
            line = line + " PROBLEM " + at.problem + " fields " + string(at.fields)
        end if
        print(line)
    end for
    e = error
    if e then
        print("  RAISED " + e.message)
        if count(keys(e.details)) > 0 then
            print("  details " + string(e.details))
        end if
    end if
end function

show("plain", "a,b\n1,2\n3,4\n", {})
show("no final line break", "a,b\n1,2", {})
show("quoted delimiter", "a,b\n\"x,y\",2\n", {})
show("quoted line break spans lines", "a,b\n\"x\ny\",2\n3,4\n", {})
show("doubled quote", "a\n\"say \"\"hi\"\"\"\n", {})
show("quoted empty and unquoted empty", "a,b,c\n\"\",,x\n", {})
show("crlf endings", "a,b" + CR + "\n1,2" + CR + "\n3,4" + CR + "\n", {})
show("lone cr endings", "a,b" + CR + "1,2" + CR + "3,4", {})
show("crlf inside quotes kept as written", "a\n\"x" + CR + "\ny\"\n", {})
show("bom", chr(65279) + "a,b\n1,2\n", {})
show("bom only at the start", "a,b\n" + chr(65279) + "1,2\n", {})
show("blank lines skipped and counted", "a,b\n\n1,2\n\n\n3,4\n", {})
show("a line of delimiters is not blank", "a,b\n,\n", {})
show("quote inside an unquoted field", "a,b\n5\" pipe,2\n", {})
show("not trimmed", "a, b\n1, 2\n", {})
show("nul is content", "a\nx" + chr(0) + "y\n", {})
show("no header", "1,2\n3\n", { header: false })
show("names", "1,2\n3,4\n", { names: ["x", "y"] })
show("tab", "a\tb\n1\t\"2\t3\"\n", { delimiter: "\t" })
show("pipe", "a|b\n1|2\n", { delimiter: "|" })
show("no quoting", "a,b\n\"1,2\n", { quote: "" })
show("other quote", "a,b\n'x,y',2\n", { quote: "'" })
show("empty text", "", {})
show("header only", "a,b\n", {})
show("long record raises", "a,b\n1,2\n3,4,5\n6,7\n", {})
show("short record raises", "a,b\n1,2\n3\n", {})
show("text after a closing quote raises", "a,b\n\"x\"y,2\n", {})
show("deliver", "a,b\n1,2,3\n4\n\"x\"y,5\n6,7\n", { problems: "deliver" })
show("unterminated quote names its line", "a,b\n1,2\n\"open,\nmore\nstill\n", {})
show("unterminated quote, delivered too", "a,b\n\"open\n", { problems: "deliver" })
show("empty header name", "a,,c\n1,2,3\n", {})
show("duplicate header name", "a,b,a\n1,2,3\n", {})
show("case kept in names", "Price,price\n1,2\n", {})
show("header with text after a quote", "\"a\"b,c\n1,2\n", {})
show("unknown option", "a\n1\n", { delimeter: ";" })
show("delimiter too long", "a\n1\n", { delimiter: ";;" })
show("delimiter a line break", "a\n1\n", { delimiter: "\n" })
show("delimiter same as quote", "a\n1\n", { delimiter: "\"" })
show("names beside header true", "a\n1\n", { header: true, names: ["x"] })
show("duplicate names", "1,2\n", { names: ["x", "x"] })
show("problems misspelt", "a\n1\n", { problems: "skip" })
show("header not a boolean", "a\n1\n", { header: "yes" })

function refuse(label, f)
    on error goto next
    x = f()
    e = error
    if e then
        print("-- " + label + ": RAISED " + e.message)
    end if
end function

function parse_a_number()
    return csv.parse(5)
end function
function records_of_a_number()
    return csv.records(5)
end function
function options_not_a_record()
    return csv.parse("a", [1])
end function
function no_such_verb()
    return csv.read("a")
end function
function source_as_condition()
    s = csv.parse("a\n1\n")
    if s then
        return 1
    end if
    return 0
end function
function missing_field_of_short_record()
    for each r, at in csv.parse("a,b\n1\n", { problems: "deliver" })
        print(r.b)
    end for
    return 0
end function
function missing_file()
    for each r in csv.records({file}"/nonexistent/csv/file.csv")
        print(r)
    end for
    return 0
end function

refuse("csv.parse of a number", parse_a_number)
refuse("csv.records of a number", records_of_a_number)
refuse("options not a record", options_not_a_record)
refuse("no such verb", no_such_verb)
refuse("a source as a condition", source_as_condition)
refuse("a short record's missing field", missing_field_of_short_record)
refuse("a missing file", missing_file)

' A source is a description: looping twice reads twice, and a copy is the same.
s = csv.parse("a\n1\n2\n")
n = 0
for each r in s
    n = n + 1
end for
for each r in s
    n = n + 1
end for
t = s
print("-- twice: " + string(n) + ", copy equal " + string(t = s) + ", type " + type(s) + ", renders " + string(s))
