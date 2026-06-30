' frame.bas data-frame layer (statistics_design.md §4). A frame is a record of
' equal-length columns; every transform returns a new frame. Covers inspect,
' conversions, select/reshape, clean, group/summarize, join, and CSV IO.
function is_adult(row)
    return row.age >= 18
end function
function decade(row)
    return floor(row.age / 10) * 10
end function
function g_n(g)
    return len(g.age)
end function
' Aggregators are arbitrary functions of the group sub-frame. max keeps the
' golden exact (record serialization prints numbers at full precision, so a
' computed float like a mean would show its last bits); mean(df.col) is covered
' by the other stats tests.
function g_max_age(g)
    return max(g.age)
end function

program demo(args)
    load frame from "../stdlib/frame.bas"

    people = { name: ["Ada", "Grace", "Alan", "Ada"], age: [36, 45, 17, 36], city: ["NYC", "LA", "NYC", "NYC"] }

    ' --- Inspect ---
    print("shape " + string(frame.shape(people)))
    print("columns " + join(frame.columns(people), ","))
    print("dtypes " + string(frame.dtypes(people)))
    print("head2 " + string(frame.head(people, 2)))

    ' --- Conversions ---
    rows = frame.to_rows(people)
    print("row0 " + string(rows[0]))
    print("roundtrip " + string(serialize(frame.from_rows(rows)) = serialize(people)))

    ' --- Select / reshape ---
    print("select " + string(frame.select(people, ["name", "age"])))
    print("drop " + string(frame.drop(people, ["city"])))
    print("rename " + join(frame.columns(frame.rename(people, {age: "years"})), ","))

    ' --- Clean ---
    print("filter " + string(frame.filter(people, is_adult)))
    print("with_col " + string(frame.with_column(people, "decade", decade).decade))
    print("intact " + string(has(people, "decade")))

    miss = { a: [1, unknown, 3], b: ["x", "y", unknown] }
    print("fill " + string(frame.fill_missing(miss, "a", 0).a))
    print("dropna " + string(frame.drop_missing(miss)))
    print("coerce " + string(frame.coerce({ v: ["1", "2", "bad"] }, "v", "number").v))

    ' --- Dedupe / sort ---
    print("dedupe " + string(frame.shape(frame.dedupe(people))))
    print("sorted " + string(frame.sort_by(people, "age").age))

    ' --- Group / summarize ---
    s = frame.summarize(people, "city", { n: g_n, max_age: g_max_age })
    print("summarize " + string(s))

    ' --- Join (inner, with collision suffix) ---
    left = { id: [1, 2, 3], name: ["Ada", "Bob", "Cy"] }
    right = { id: [2, 3, 3], score: [88, 90, 91] }
    print("join " + string(frame.join(left, right, "id")))

    ' --- CSV import/export (type inference, ID-like as string, empty -> NA) ---
    csv = "name,age,zip,score" + chr(10)
    csv = csv + "Ada,36,07030,9.5" + chr(10)
    csv = csv + "Grace,45,10001," + chr(10)
    csv = csv + "Alan,,90210,7.0" + chr(10)
    w(file)= "examples/tmp_frame_in.csv"
    write(w, csv)

    df = frame.read_csv("examples/tmp_frame_in.csv")
    print("csv_dtypes " + string(frame.dtypes(df)))
    print("csv_age " + string(df.age))
    print("csv_zip " + string(df.zip))
    print("csv_score " + string(df.score))

    frame.write_csv(df, "examples/tmp_frame_out.csv")
    df2 = frame.read_csv("examples/tmp_frame_out.csv")
    print("csv_roundtrip " + string(serialize(df2) = serialize(df)))

    ' --- show ---
    print("--- show ---")
    frame.show(frame.select(df, ["name", "age", "score"]))

    ' clean up temp files
    f1(file)= "examples/tmp_frame_in.csv"
    f2(file)= "examples/tmp_frame_out.csv"
    delete(f1)
    delete(f2)
end program
