' Recipe 8 — Writing, and the two ways it can go wrong.
'
' Reading a file you were sent is forgiving: whatever arrived, arrived. Writing
' one you will SEND is not. A pain.001 credit transfer goes to a bank, and a
' creditor name three characters too long does not come back as an error — it
' comes back as a payment to a truncated name, or silently not at all.
'
' So §16 asks BEFORE serialising anything, and there are three answers:
'
'   representable  every value fits what the target carries
'   lossy          it would fit if something were shortened
'   impossible     the target format does not carry this at all
'
' The default favours refusal. A LOSSY write is refused unless you say
' otherwise — the loss is reported either way, so it can be permitted but not
' silent. An IMPOSSIBLE write has NO override, because that is a fact about the
' target format and not a cost you can choose to accept.
'
' Note what is being checked: the SCHEME's limits, not the schema's. The XSD
' would accept a 140-character creditor name. SEPA carries 70. A document that
' validates against the schema and gets rejected by the bank is the ordinary
' case, not the exotic one.
program main()
    load finio
    load finio_all
    load finio_pain001

    reg = finio_all.registry()

    print ("the scheme's limits: " + string(finio_pain001.scheme_limits()))
    print ""

    for each name in [ "payments.xml", "long_name.xml", "mixed_currency.xml" ]
        doc = finio.read_file(reg, "tests/finio/pain/" + name, {})
        c = finio.classify_write(reg, doc)
        print (name + "  ->  " + c.classification)
        print ("  " + string(c.why))

        ' Try to write it the ordinary way and see what happens.
        on error goto next
        out = finio.write_text(reg, doc)
        if error then
            print ("  refused: " + error.message)
            error.clear()
        else
            print ("  written: " + string(byte_count(out.text)) + " bytes, "
                   + "byte fidelity " + string(out.byte_fidelity))
        end if
        on error stop
        print ""
    end for

    ' The lossy one, permitted. The loss is still reported.
    doc = finio.read_file(reg, "tests/finio/pain/long_name.xml", {})
    out = finio.write_text(reg, doc, true)
    print ("long_name.xml written with allow_lossy: "
           + string(byte_count(out.text)) + " bytes")
    print ("  classification stays " + out.classification
           + ", loss notes " + string(count(out.loss)))

    ' And the impossible one stays impossible, however hard you ask.
    doc = finio.read_file(reg, "tests/finio/pain/mixed_currency.xml", {})
    on error goto next
    out = finio.write_text(reg, doc, true)
    if error then
        print "mixed_currency.xml with allow_lossy: still refused"
        error.clear()
    else
        print "mixed_currency.xml with allow_lossy: WROTE IT (this would be a bug)"
    end if
    on error stop
end program
