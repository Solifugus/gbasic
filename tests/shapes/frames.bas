' docs/shapes_design.md §6.4, increment 5: a frame typed by a shape. Every
' expected line in frames.out was worked out by hand from the design. Nothing
' here warns (the warnings and refusals are run_shapes.sh's frames tier). The
' runner hands a scratch directory as args[0].
load frame
program main(args)
    f = args[0] + "/in.csv"
    write({file}f, "Paid,AMOUNT,Invoice Date,invoice_id\ntrue,12.50,2026-01-05,007\n,3,2026-01-06,A2\nfalse,,2026-01-07,\n")

    ' read by the shape: the header matched as csv.records matches it, the
    ' columns in declared order, each cell its field's type, an empty cell
    ' nothing outside text and "" in it
    df = frame.read_csv(f, invoice)
    print frame.columns(df)
    print frame.dtypes(df)
    print df.invoice_id
    print df.amount
    print df.paid

    ' the same file guessed: 12.50 is a number, "true" text, an empty cell unknown
    u = frame.read_csv(f)
    print u.AMOUNT
    print u.Paid

    ' rows are invoices, the same ones csv.records reads, and back again
    rows = frame.to_rows(df, invoice)
    print rows[0]
    r:invoice = rows[1]
    print type(r.amount) + " " + string(r.amount)
    direct = []
    for each d in csv.records(f, invoice)
        append(direct, d)
    end for
    print rows = direct
    print frame.from_rows(rows) = df

    ' a frame from elsewhere put into the shape: names matched exactly, so the
    ' guessed frame is renamed first; a field with no column takes its default
    c = frame.conform(frame.rename(u, { Paid: "paid", AMOUNT: "amount", "Invoice Date": "invoice_date" }), invoice)
    print frame.columns(c)
    print c.amount
    print c.paid
    print c.invoice_date
    print frame.conform({ invoice_id: ["x"], invoice_date: ["2026-03-01"], amount: [1] }, invoice).paid
    print frame.conform({ invoice_id: [], invoice_date: [], amount: [], paid: [] }, invoice)

    ' a shaped frame's missing cells are nothing, and the missing-cell verbs see them
    print frame.drop_missing(df).invoice_id
    print frame.fill_missing(df, "paid", false).paid
    print frame.coerce(df, "amount", "string").amount

    ' convert: the lock when the shape is a value
    s = invoice
    print convert({ invoice_id: "z", invoice_date: "2026-04-01", amount: "2" }, s)
    print convert(nothing, s)
    print convert(new invoice, s) = new invoice
    print convert(new other, s)
end program

shape invoice
    invoice_id:text
    invoice_date:date
    amount:money(USD)
    paid:boolean = false
end shape

shape other
    invoice_id:text = "o1"
    invoice_date:date = "2026-05-01"
    amount:number = 4
end shape
