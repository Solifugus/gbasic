' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' finio_all -- every finio adapter this build carries, in one list.
'
' WHY THIS EXISTS, and it is a measured reason rather than a convenience. The
' adapter list was built by hand at every call site, and adding an adapter
' meant finding all of them. It was missed THREE TIMES in one day: the registry
' fixture, the registry runner's two embedded programs, and the watch runner's
' -- each failing later and separately, each reporting a count rather than the
' cause. That is the drift `tools` exists to prevent one library over and the
' defect `web.configure`'s tripwire guards: one fact written in several places.
'
' IT IS ALSO WHAT AN APPLICATION ACTUALLY WANTS. §8's archive sweep asks which
' of a directory's files are recognisable, and that question needs EVERY
' adapter -- naming four by hand is friction at the one call site where the
' answer depends on not forgetting any.
'
' THE COUPLING IS REAL AND IS THE POINT OF HAVING A CHOICE: loading this loads
' every adapter, and a program that reads only ACH files should `load
' finio_nacha` and build its own one-element registry. That is the same
' argument `tools` made for refusing `via: {mcp:}` -- a convenience that drags
' in what a caller does not use is not free -- and the difference here is that
' the convenience has a caller whose whole purpose is breadth.

library finio_all

load finio from "finio.bas"
load finio_nacha from "finio_nacha.bas"
load finio_camt from "finio_camt.bas"
load finio_bai2 from "finio_bai2.bas"
load finio_ofx from "finio_ofx.bas"
load finio_pain001 from "finio_pain001.bas"

function adapters()
    return [ finio_nacha.adapter(),
             finio_camt.adapter(),
             finio_bai2.adapter(),
             finio_ofx.adapter(),
             finio_pain001.adapter() ]
end function

function registry()
    return finio.registry(adapters())
end function

' The ids, so a caller can report what it holds without constructing anything.
function ids()
    out = []
    for each a in adapters()
        append(out, a.id)
    end for
    return out
end function
end library
