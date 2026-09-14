program main(args)
    load gpdf
    when {datetime}= "2026-01-01 12:00:00"
    d = gpdf.document({ created: when, title: "Letterhead" })
    d = gpdf.add_page(d)
    d = gpdf.image(d, "docs/assets/mascot.png", 72, 620, { width: 300 })
    d = gpdf.set_font(d, "Helvetica-Bold", 16)
    d = gpdf.text_at(d, 72, 590, "gBASIC Consulting")
    d = gpdf.set_font(d, "Helvetica", 11)
    d = gpdf.text_at(d, 72, 570, "Invoice 1041")
    print string(gpdf.save(d, args[0] + "/logo.pdf"))
end program
