' csv.line and csv.write (docs/csv_design.md §6). The runner hands a scratch
' directory as the first argument.
function try_line(label, values, opts)
    on error goto next
    t = csv.line(values, opts)
    e = error
    if e then
        print(label + ": RAISED " + e.message)
    else
        print(label + ": " + json_encode(t))
    end if
end function

function try_write(label, f, rows, opts)
    on error goto next
    csv.write(f, rows, opts)
    e = error
    if e then
        print(label + ": RAISED " + e.message)
    else
        print(label + ": " + json_encode(read(f)))
    end if
end function

program main(args)
    dir = args[0]
    try_line("plain", ["a", "b"], {})
    try_line("needs quoting", ["a,b", "say \"hi\"", "two" + chr(10) + "lines", "cr" + chr(13), "ok"], {})
    try_line("spaces are not quoted", [" a ", ""], {})
    try_line("one empty field", [""], {})
    try_line("one empty field, no quoting", [""], { quote: "" })
    try_line("record in its own order", { b: "2", a: "1" }, {})
    try_line("names order a record", { b: "2", a: "1" }, { names: ["a", "b"] })
    try_line("typed values", [3, 2.5, {USD}"1.50", {date}"2026-11-02", true, unknown, nothing], {})
    try_line("exact integer", [12345678901234567], {})
    try_line("money keeps every digit it holds", [{USD}"1250.7512", {USD}"3", {JPY}"1995", {USD}"-0.005"], {})
    try_line("tab", ["a" + chr(9) + "b", "c"], { delimiter: chr(9) })
    try_line("crlf", ["a", "b"], { line_end: chr(13) + chr(10) })
    try_line("other quote", ["it's", "a,b"], { quote: "'" })
    try_line("no quoting, plain", ["a", "b"], { quote: "" })
    try_line("no quoting, needs it", ["a,b"], { quote: "" })
    try_line("nested array", ["a", [1, 2]], {})
    try_line("nested record", [{ x: 1 }], {})
    try_line("not fields", "abc", {})
    try_line("line_end other", ["a"], { line_end: ";" })
    try_line("reader option", ["a"], { header: false })
    try_line("missing named field", { a: "1" }, { names: ["a", "b"] })
    try_line("misnamed field", { a: "1", c: "2" }, { names: ["a", "b"] })

    out{file}= join_path(dir, "out.csv")
    rows = [{ sku: "A-100", description: "Gripper, two-finger", price: {USD}"120.50" }, { sku: "A-200", description: "Servo motor" + chr(10) + "24V, 400W", price: {USD}"310.00" }]
    try_write("records", out, rows, {})
    try_write("names choose the header", out, rows, { names: ["price", "sku", "description"] })
    try_write("arrays need names", out, [["1", "2"], ["3", "4"]], { names: ["x", "y"] })
    try_write("arrays with no header", out, [["1", "2"], ["3", "4"]], {})
    try_write("a shifted record", out, [{ a: "1", b: "2" }, { a: "3", c: "4" }], {})
    try_write("a short record", out, [{ a: "1", b: "2" }, { a: "3" }], {})
    try_write("a record after arrays", out, [["1"], { a: "2" }], {})
    try_write("not rows", out, "abc", {})
    try_write("no rows", out, [], {})

    ' and back: what csv.write wrote, csv.records reads
    csv.write(out, rows, {})
    for each item, at in csv.records(out)
        print("read back line " + string(at.line) + ": " + json_encode(item))
    end for
end program
