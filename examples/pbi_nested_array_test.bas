' PBI §6: a nested instance re-derives WHEREVER IT SITS -- in a record field or
' inside an array. §6 decided recursive derivation and then named two mechanics
' "still to pin down during implementation"; arrays were the unfinished half.
'
' MEASURED 2026-09-30, before the fix: `motor (copy): new engine` gave two cars
' serials 2 and 3, while `motors (copy): [new engine]` gave BOTH 4. Same value,
' same policy, different answer decided by whether a bracket was in the way --
' which is the silent no-op §6 rejected flat duplication to avoid, arriving by
' the back door.
'
' SELF-CHECKING, not a bare transcript: every defect here is a PLAUSIBLE SERIAL
' NUMBER. A golden would have recorded `4` and `4` as expected and defended it.
serials = []
function new_serial()
    append(serials, "s")
    return count(serials)
end function

function check(label, got, want)
    if got = want then
        print("ok   " + label)
    else
        print("MISMATCH " + label + ": got " + string(got) + ", want " + string(want))
    end if
end function

engine = { serial (reset new_serial()): 0, rpm (copy): 0 }

' --- §6's own example: an instance in a RECORD field --------------------
car = { make (copy): "Forda", motor (copy): new engine }   ' serial 1
c1 = new car
c2 = new car
check("record field re-derives (c1)", c1.motor.serial, 2)
check("record field re-derives (c2)", c2.motor.serial, 3)

' --- THE CASE THIS FILE EXISTS FOR: the same instance in an ARRAY -------
fleet = { motors (copy): [new engine] }                    ' serial 4
f1 = new fleet
f2 = new fleet
check("array element re-derives (f1)", f1.motors[0].serial, 5)
check("array element re-derives (f2)", f2.motors[0].serial, 6)

' --- and all the way down, because §6 says it follows the data ----------
depot = { racks (copy): [[new engine]] }                   ' serial 7
d1 = new depot
d2 = new depot
check("nested array re-derives (d1)", d1.racks[0][0].serial, 8)
check("nested array re-derives (d2)", d2.racks[0][0].serial, 9)

' --- CONTROL: an array with NO instance in it is untouched --------------
' Without this, "arrays are re-derived" is satisfied by rebuilding every array
' field eagerly, which would throw away the copy-on-write share that makes the
' recursion cheap -- and §6 rests on that being cheap.
plain = { tags (copy): ["x", "y"] }
p1 = new plain
append(p1.tags, "z")
check("plain array still forks on write (source)", count(plain.tags), 2)
check("plain array still forks on write (copy)", count(p1.tags), 3)

' --- CONTROL: the reset policy PERSISTS, so an instance re-derives again -
' A derived instance keeps policy = RESET, so using it as a prototype fires the
' reset once more. This is what separates "re-derived" from "copied twice".
g1 = new f1
check("a derived instance is still a prototype", g1.motors[0].serial, 10)

print("new_serial calls: " + string(count(serials)))
