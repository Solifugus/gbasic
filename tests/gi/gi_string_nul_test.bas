' A GObject string argument cannot carry an interior NUL, so it is REFUSED
' rather than truncated (PLAT-NUL's door sweep, 2026-09-25).
'
' Measured before the check existed: gi.invoke("GLib.Uri.escape_string",
' "a" + chr(0) + "b", nothing, false) answered ONE byte -- hex 61 -- in
' silence, because a GI `utf8` argument is a NUL-terminated C string by
' construction and there is no length to pass alongside it.
'
' THIS IS XML's ANSWER AND NOT SQLITE's, and the difference is a fact about the
' far side rather than a preference: the databases and the HTTP bodies were
' repaired because those interfaces take a byte count, and this one does not,
' so there is nothing to repair and saying so is the only honest option.
'
' THE MESSAGE IS PART OF THE ASSERTION. A bare `return 0` from the conversion
' would have produced the caller's generic "unsupported argument type for
' method", which is FALSE -- the type is supported and the value is not -- so
' the specific message is raised at the conversion and survives because a raise
' already in flight wins. A tier asserting only "it refused" would pass on the
' wrong sentence.
load gi

function escape(s)
    on error goto bad
    return "escaped: " + gi.invoke("GLib.Uri.escape_string", s, nothing, false)
bad:
    m = error.message
    error.clear()
    return "refused: " + m
end function

program main(args)
    gi.require("GLib", "2.0")
    print escape("a" + chr(0) + "b")
    ' THE CONTROL, or "it refuses a NUL" is equally satisfied by a bridge that
    ' has stopped accepting strings at all.
    print escape("ab")
    ' And a string whose NUL is ENCODED is an ordinary string: the remedy the
    ' message names has to work.
    print escape(hex_encode("a" + chr(0) + "b"))
end program
