' PLAT-GUARD: the behavioural half of the pre-registration tripwire.
'
' Everything the program block reaches here is declared BELOW `end program`. If the
' interpreter stopped pre-registering any of these kinds, this child stops running
' -- which is the same failure gBASIC Studio's STU-4B hoisting rule would suffer,
' surfaced at the platform level where the rule's premise actually lives.
'
' The negative half matters just as much: a top-level executable statement below
' the block must NOT run. That is what makes hoisting declarations safe -- there is
' no executable effect out there to reorder.
'
' The fourth registered thing, dotted-def method bodies, has no effect a program
' block can observe (reaching one still needs a receiver record, and the attaching
' statement never runs) -- which is exactly what STU-4B relies on when it calls
' them inert. One is declared below anyway, so its presence is proven harmless;
' the registration itself is asserted structurally by the runner.
program main()
    ' 1. A plain top-level function declared after the block.
    print "function=" + doubled(21)

    ' 2. A modifier declared after the block.
    shouty{loud}= "quiet"
    print "modifier=" + shouty

    ' 3. A library declared after the block, resolved from the root AST.
    load helper
    print "library=" + helper.tag()

    ' 4. A function VALUE taken from a function declared after the block.
    ref = doubled
    print "function-value=" + ref(5)

    ' 5. A server block declared after the program block (PLAT-WEB-5): the
    '    name is bound to its inert value and its handlers are callable as
    '    function values, before anything below the block ever ran.
    print "server=" + late.name + "/" + string(count(late.sites[0].routes))
    h = late.sites[0].routes[0].handler
    r = h({ id: 1, method: "GET", path: "/" })
    print "server-handler=" + r.body

    ' 6. The negative: nothing out there executed.
    print "top-level-ran=" + top_level_ran()
end program

' --- everything below here is written after the block on purpose ---------------

' A top-level executable statement. It must never run: were it to, this line would
' appear in the output and the transcript would not match.
print "TOP-LEVEL-STATEMENT-MUST-NOT-RUN"

function doubled(n)
    return n * 2
end function

modifier loud for assign
    return upper(value)
end modifier

' A dotted (attached) definition: its body registers, its attachment does not run.
function holder.describe()
    return "described"
end function

function top_level_ran()
    return false
end function

' PLAT-WEB-5: a server declaration below the block, reached from inside it.
server late( port: 0 )
    get "/"( req )
        return { body: "hoisted" }
    end get
end server

library helper
    function tag()
        return "helper-tag"
    end function
end library
