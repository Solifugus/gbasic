' Recipe 3 — A blank field and a broken field are different answers.
'
' This is Axiom 7, and it is the recipe most likely to prevent a real defect.
'
' A field that is BLANK means the source said nothing: `unknown`. A field that
' is PRESENT and fails its own rule means the source said something that cannot
' be what it claims: `invalid`. Collapsing them loses the distinction the
' person reading the report needs — one is a gap to go and fill, the other is a
' defect to send back.
'
' The direction that actually hurts is reading a broken amount as ZERO. It
' understates the file, and an understated file looks exactly like a small one.
' So `value` is `unknown` in both cases — never 0, never "" — and the raw bytes
' survive either way so a human can see what was really there.
program main()
    load finio
    load finio_all

    reg = finio_all.registry()
    f {file}= "tests/finio/nacha/payroll.ach"
    payroll = read(f)

    doc = finio.read_text(reg, payroll, {})

    print "-- the file as it arrived"
    fld = doc.records[0].fields.reference_code
    print ("  reference_code   status " + fld.status
           + ", value is unknown: " + string(is_unknown(fld.value))
           + ", raw [" + fld.raw + "]")
    fld = doc.records[2].fields.individual_name
    print ("  individual_name  status " + fld.status
           + ", value [" + string(fld.value) + "]"
           + ", raw [" + fld.raw + "]")
    print ""

    ' Now damage one amount: an X where a digit belongs. This is what a bad
    ' export, a truncated transfer or a hand-edited file looks like.
    broken = replace(payroll, "0000125000EMP0001", "00001X5000EMP0001")
    bdoc = finio.read_text(reg, broken, {})

    print "-- the same file with one character changed"
    fld = bdoc.records[2].fields.amount
    print ("  amount           status " + fld.status)
    print ("  value is unknown: " + string(is_unknown(fld.value))
           + "   (and NOT zero: " + string(fld.value = 0) + ")")
    print ("  raw [" + fld.raw + "]  — the bytes are kept, so you can see what was there")
    print ("  why: " + string(fld.why))
    print ""

    ' The file still READ. Nothing raised. That is deliberate — see recipe 4.
    print ("records read: " + string(count(bdoc.records)))
    print ("issues found by validate: " + string(count(finio.validate(reg, bdoc))))
end program
