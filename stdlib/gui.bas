' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' gui.bas -- INTENTIONALLY EMPTY. The declarative `gui` module is NATIVE: it
' lives in src/eval.c behind `#if HAVE_GTK` and is dispatched by name, so there
' is nothing for a gBASIC library to hold. This file exists so that `load gui`
' resolves at all.
'
' READ THIS BEFORE BUILDING ON IT. The module is GTK 3, docs/README.md marks it
' "Partial -- an experimental proof of concept. Prefer `gi` for new work", and
' `load gui` appears NOWHERE in this tree: not stdlib, not examples, not tests.
' gBASIC Studio, the largest GTK application written in gBASIC, calls `gi` 158
' times and `gtk` 119 times and this module zero.
'
' What it does own is the IDENTITY RULES that make a widget tree addressable --
' every widget needs an `id`, ids are unique within a window, and an id must be
' exposable as `win.<id>` (so `win.cancel` resolves and `win.row.cancel` does
' not; lookup is flat). docs/gui_addressability_design.md §0.3 decided those are
' the rules an addressable layer should keep, whichever backend renders it.

library gui
end library
