' PLAT-MONEY phase 2: SER_VERSION 2, and the v1 migration.
'
' A representation change to a serializable value needs a versioning decision,
' not a silent reinterpretation. Version 1 stored a bare int64 of cents;
' version 2 stores units, currency AND exponent, so a value is
' self-describing -- which matters because actors are fork+exec, so a currency
' registered in the parent is not registered in the child.

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

' ------------------------------------------------------- v2 round trip
u {USD}= "1234.56"
back = deserialize(serialize(u))
check("USD survives a round trip", back, "1234.56")
check("and is still money", type(back), "money")

j {JPY}= "1995"
jb = deserialize(serialize(j))
check("JPY survives with its exponent", jb, "1995")

k {KWD}= "19.950"
kb = deserialize(serialize(k))
check("KWD survives with three places", kb, "19.950")

' The currency must survive, not just the number -- otherwise a deserialized
' value would silently become addable to the wrong thing.
on error goto next
x = jb + u
check("a deserialized JPY still refuses USD", error.message, "cannot add money in different currencies (JPY and USD)")
error.clear()
on error stop

' Guard digits survive too: a sub-minor-unit intermediate must not be flattened
' by a trip through the wire.
cent {USD}= "0.01"
third = cent / 3
tb = deserialize(serialize(third))
check("guard digits survive serialization", tb * 3, "0.01")

' A REGISTERED currency: the child of an actor has no such registration, so the
' exponent has to ride in the value itself.
money.register("PTS", 0)
p {PTS}= "1500"
pb = deserialize(serialize(p))
check("a registered currency survives", pb, "1500")

' ------------------------------------------------- the v1 migration
' Read from a payload the PHASE-1 BINARY actually wrote (tests/money/
' v1_payload.hex), not from bytes this version generated to look old.
f {file}= env("GBASIC_MONEY_V1")
old = deserialize(hex_decode(read(f)))
check("a v1 payload still deserializes", count(old), 3)
check("its money is rescaled into guard digits", old[0], "1234.56")
check("and is money, not a number", type(old[0]), "money")
check("v1 money is assumed USD", string(old[0] + u), "2469.12")
check("the rest of the payload is unharmed", old[1], "tag")
check("including numbers", old[2], 42)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
