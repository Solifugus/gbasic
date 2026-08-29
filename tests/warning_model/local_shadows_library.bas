' PLAT-WARN: a local function shadowing a LIBRARY function.
'
' Two defects in one scenario, reported by the Transward build.
'
' 1. THE QUALIFIER DID NOT ESCAPE THE COLLISION. Importing a library function
'    whose name matched an existing local RETURNED WITHOUT REGISTERING IT, so
'    `shadowlib.start_server(...)` failed with "invalid function call" -- and
'    the qualified call is precisely what one reaches for when a name collides.
'    The diagnostic pointed at the CALL, so the reader went looking inside a
'    library for a function that was there all along.
'
' 2. Nothing warned. The library-versus-builtin collision has warned for a long
'    time; local-versus-library is the same shape one level over and was
'    silent.
'
' NOTE the `load` is INSIDE the program block on purpose: `load` is an
' executable statement, so a top-level one never runs -- which is its own
' warning, exercised by top_level_load.bas.

function start_server(port)
    return "local:" + string(port)
end function

program main(args)
    load shadowlib
    ' The local keeps UNQUALIFIED calls. That precedence is deliberate.
    print start_server(1)
    ' And the qualifier reaches the library one. That is the fix.
    print shadowlib.start_server(2)
end program
