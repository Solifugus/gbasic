' PBI §6/§10.1: WHICH LEVELS DOES A `new` RUN CODE AT?
'
' §6 decided recursive derivation and named two mechanics "still to pin down
' during implementation". Arrays were one half (pbi_nested_array_test.bas); this
' is the other, and the question the gbasic-books session asked while drafting
' Volume 2 Chapter 10: does a `constructor` make a nested record an "instance"
' that re-derives, and does that constructor fire again?
'
' RULED 2026-10-02 (Matthew): (b) -- POLICIES RECURSE, CONSTRUCTORS DO NOT.
'
' THE REASON IS THE DIFFERENCE IN KIND, not a preference about depth. A `reset`
' is a DECLARED PER-FIELD POLICY: a datum, idempotent, and re-firing it is the
' whole point -- §6 rejected flat duplication precisely because it turned a
' nested `reset` into a silent no-op. A `constructor` is ARBITRARY USER CODE with
' side effects: it can write a file, send a message, or take an id from a server.
' Under the other reading one `new` would run it 1 + (instances nested at any
' depth) times, a count the author cannot see from the call site and which grows
' when somebody they do not know nests their object inside something else.
'
' AND IT HAD ALREADY RUN ONCE. `motor (copy): new engine` runs the engine's
' constructor when the LITERAL is evaluated. Re-firing on derivation does not
' give each car a freshly constructed engine -- it gives the program 1 + n runs
' of a side effect that was already performed, which is the shape of a bug
' rather than of a feature.
'
' SELF-CHECKING, not a bare transcript, and here that is forced: every defect
' this file can catch is a PLAUSIBLE SMALL INTEGER -- a constructor that ran
' twice, or a serial one higher than it should be. A golden would have recorded
' whichever count came out and defended it.
builds = []
assemblies = []
serials = []

function next_serial()
    append(serials, "s")
    return count(serials)
end function

function build_engine()
    append(builds, "b")
    this.built = count(builds)
end function

function assemble_car()
    append(assemblies, "a")
    this.assembled = count(assemblies)
end function

' Read a field that may be ABSENT. Needed because both perturbations this file
' is proven against make a constructor-written field disappear, and a bare
' `c1.assembled` RAISES -- which truncates the report at its first mismatch, so
' a reader sees one line of a fourteen-line answer. A check that reports is
' worth more than a check that dies.
function field_or(rec, name, fallback)
    if has(rec, name) then
        return rec[name]
    end if
    return fallback
end function

function check(label, got, want)
    if got = want then
        print("ok   " + label)
    else
        print("MISMATCH " + label + ": got " + string(got) + ", want " + string(want))
    end if
end function

engine = {
    serial      (reset next_serial()): 0,
    constructor (link):                build_engine
}

' motor_a nests an INSTANCE (`new engine`, so the engine's constructor runs HERE,
' once, while the literal is being evaluated -- serial 1, built 1).
' motor_b nests the PROTOTYPE ITSELF, with no `new` at all.
car = {
    motor_a     (copy):  new engine,
    motor_b     (copy):  engine,
    constructor (link):  assemble_car
}

check("the literal's own `new engine` ran the constructor once", count(builds), 1)
check("and the outer constructor has not run yet", count(assemblies), 0)

c1 = new car
c2 = new car

' --- THE OUTER CONSTRUCTOR FIRES ONCE PER `new` -------------------------
' The level the author WROTE `new` at is the level that runs code. This is the
' control that stops the ruling from being satisfied by a build where
' constructors stopped firing anywhere.
check("outer constructor ran for c1", field_or(c1, "assembled", -1), 1)
check("outer constructor ran for c2", field_or(c2, "assembled", -1), 2)
check("outer constructor ran exactly twice", count(assemblies), 2)

' --- THE NESTED CONSTRUCTOR DOES NOT ------------------------------------
' This is the ruling. `builds` is still 1: the one run the literal performed.
check("nested constructor never re-fired", count(builds), 1)
check("c1.motor_a.built is the literal's value", field_or(c1.motor_a, "built", -1), 1)
check("c2.motor_a.built is the same value", field_or(c2.motor_a, "built", -1), 1)

' --- CONTROL: THE NESTED `reset` STILL RE-FIRES -------------------------
' Without this, "nested constructors do not fire" is equally satisfied by a
' build in which nested derivation does not happen at all -- which is the
' reading §6 already rejected, arriving by the back door.
check("nested reset re-fired for c1", c1.motor_a.serial, 2)
check("nested reset re-fired for c2", c2.motor_a.serial, 4)

' --- CONTROL: A NESTED PROTOTYPE DERIVES IDENTICALLY --------------------
' `new` inside a literal buys the constructor's ONE-TIME effect and nothing
' about derivation: motor_b was never `new`ed, and its reset re-fires exactly as
' motor_a's does. So the discriminator for recursion is the POLICY, and the
' presence of a `constructor` field says nothing about it either way.
check("nested prototype re-derives too (c1)", c1.motor_b.serial, 3)
check("nested prototype re-derives too (c2)", c2.motor_b.serial, 5)
check("and it has no constructor-written field", has(c2.motor_b, "built"), false)

' --- THE COUNT THE OTHER READING WOULD HAVE MADE UNBOUNDED --------------
' Two `new car` calls over an object nesting two engines: under re-firing this
' would be 1 + 4. It is 1.
check("total engine constructor runs for two cars", count(builds), 1)

print("engine constructor runs: " + string(count(builds)))
print("car constructor runs: " + string(count(assemblies)))
print("serials issued: " + string(count(serials)))
