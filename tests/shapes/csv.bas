' docs/shapes_design.md §6.1, increment 3: a delimited file read and written
' through a shape. Every expected line in csv.out was worked out by hand from
' the design. The shapes are declared below the program block, which reads
' them: they are hoisted. Nothing here warns (the warnings and refusals are
' run_shapes.sh's csv tier). The runner hands a scratch directory as args[0].
program main(args)
    dir = args[0]

    ' the header is matched once: by name, then with case, spaces and _ ignored;
    ' each cell converts into its field, and a field with no column takes its default
    text = "Invoice Date,AMOUNT,customer\n2026-01-05,12.50,Ann\n2026-01-06,3,Bo\n"
    total = {USD}"0"
    for each inv, at in csv.parse(text, invoice)
        print string(at.line) + " " + inv.customer + " " + string(inv.invoice_date) + " " + string(inv.amount) + " " + string(inv.paid)
        total = total + inv.amount
    end for
    print total

    ' an empty cell in a field of a type other than text is nothing; in text it is ""
    for each inv in csv.parse("invoice_date,amount,customer\n2026-01-07,,\n", invoice)
        print inv.amount
        print len(inv.customer)
    end for

    ' a file, with options after the shape; the element is an instance, so a lock passes it
    f = dir + "/in.tsv"
    write({file}f, "customer\tamount\tinvoice_date\tpaid\nCy\t7.25\t2026-02-01\ttrue\n")
    for each got:invoice in csv.records(f, invoice, { delimiter: "\t" })
        print got.customer + " " + string(got.amount) + " " + string(got.paid)
        print got = { invoice_date: {date}"2026-02-01", amount: {USD}"7.25", customer: "Cy", paid: true }
    end for

    ' names: for a file with no header, matched as a header is
    for each inv in csv.parse("Di,2026-03-01,4\n", invoice, { names: ["Customer", "Invoice_Date", "Amount"] })
        print inv.customer + " " + string(inv.amount)
    end for

    ' problems: "deliver" hands over a cell that does not convert, its field nothing
    bad = "invoice_date,amount,customer\n2026-04-01,lots,Ed\n2026-04-02,4,Flo\n"
    for each inv, at in csv.parse(bad, invoice, { problems: "deliver" })
        print at.ok
        print inv.customer
        print inv.amount
        print starts_with(at.problem, "line 2 (record 1), column 'amount': amount is locked to money")
    end for

    ' writing: the header from the shape, in declared order, even with no rows
    out = dir + "/out.csv"
    csv.write(out, [], invoice)
    print json_encode(read({file}out))

    ' each row converts into the shape first: text into its field, a default filled
    csv.write(out, [{ customer: "Gil", amount: "1", invoice_date: "2026-05-01" }], invoice)
    print json_encode(read({file}out))

    ' and back: what csv.write wrote through a shape, csv.records reads into it equal
    rows = []
    for each inv in csv.parse(text, invoice)
        append(rows, inv)
    end for
    csv.write(out, rows)
    print json_encode(read({file}out))
    i = 0
    for each inv in csv.records(out, invoice)
        print inv = rows[i]
        i = i + 1
    end for
end program

shape invoice
    invoice_date:date
    amount:money(USD)
    customer:text
    paid:boolean = false
end shape
