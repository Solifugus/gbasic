' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' `ocr` -- reading text out of an image (docs/ocr_design.md).
'
' SELF-CHECKING NOT GOLDEN, AND FORCED. Every defect this library can have is a
' PLAUSIBLE DOCUMENT: a grid whose rows are off by one still reads like a
' report, and a golden would record whichever layout came back and defend it.
' Each check states the answer it wants and prints a MISMATCH naming both sides.
'
' THE FIXTURES ARE GENERATED, NOT PHOTOGRAPHED, and deliberately so: the real
' photographs that drove this design are one person's medical records and a
' cheque, and a fixture derived from one may not carry a line of it.
load ocr
load ari_discover

program main( args )
    G = { checks: 0, bad: 0 }

    if not ocr.available() then
        print "SKIP tesseract is not installed"
        return
    end if

    base = "examples/fixtures/ocr/"

    ' ------------------------------------------------------------------
    print "-- a page is words, never a string --"
    ' THE DEFECT THIS LIBRARY EXISTS FOR: plain tesseract recognises every word
    ' and REORDERS them into separate blocks, so an account and its amount end
    ' up twelve lines apart. Words with boxes are what make that detectable.
    p = ocr.read_page(base + "report.png")
    check("the engine ran", p.ok, true)
    check("and answered words", count(p.words) >= 20, true)
    w = p.words[0]
    check("a word carries its text", has(w, "text"), true)
    check("and its confidence", has(w, "confidence"), true)
    check("and its box", has(w, "left") and has(w, "width"), true)

    ' ------------------------------------------------------------------
    print ""
    print "-- the grid puts each row back together --"
    ' THE LOAD-BEARING CHECK. Two plausible integers prove nothing and neither
    ' does "a grid came back": what separates a correct layout from a
    ' misattributing one is whether an ACCOUNT sits on the same line as ITS OWN
    ' amount. Asserted per row, because getting one right by luck is ordinary.
    g = ocr.grid(p)
    check("781 is with its own 1,250.00", _rowhas(g, "100294781", "1,250.00"), true)
    check("782 is with its own -487.31", _rowhas(g, "100294782", "-487.31"), true)
    check("783 is with its own 9,004.55", _rowhas(g, "100294783", "9,004.55"), true)
    ' AND THE CONTROL, without which the three above pass on a grid that put
    ' every word on one line.
    check("and 781 is NOT with 782's amount", _rowhas(g, "100294781", "-487.31"), false)
    check("the total keeps its own figure", _rowhas(g, "BRANCH TOTAL", "9,767.24"), true)

    ' ------------------------------------------------------------------
    print ""
    print "-- a sideways page is read, and its boxes are moved with it --"
    ' MEASURED AS THE DOMINANT REAL FAILURE: of seven hand-held photographs five
    ' were a quarter turn or more out, and a 90-degree error reads as 87 degrees
    ' of skew at 30% confidence rather than as a rotation.
    s = ocr.read_page(base + "report_sideways.png")
    check("it reports the rotation", s.rotation = 90 or s.rotation = 270, true)
    ' THE BOXES MUST MOVE TOO. tesseract rotates internally to READ and reports
    ' boxes in the ORIGINAL frame -- measured, a sideways page reads at 78.7%
    ' while a line fit still says 81 degrees. Without the transform `grid` lays
    ' out a sideways page and manufactures the very defect this design is about.
    check("and the transformed boxes are level", _small(s.skew, 3), true)
    sg = ocr.grid(s)
    check("so the sideways page grids correctly too",
          _rowhas(sg, "100294783", "9,004.55"), true)

    ' ------------------------------------------------------------------
    print ""
    print "-- the seam: a grid is a print-image report --"
    ' THE WHOLE ARCHITECTURAL CLAIM. What comes out of `grid` is what `ari`
    ' parses and `ari_discover` infers a specification from, so OCR is a front
    ' door to existing machinery rather than a new pipeline.
    prof = ari_discover.profile(g)
    found = false
    for each fam in prof.families
        if fam.count = 3 and contains(string(fam.signature), "MONEY") then
            found = true
        end if
    end for
    check("ari_discover finds the 3-row detail family", found, true)

    ' ------------------------------------------------------------------
    print ""
    print "-- refusals --"
    on error goto next
    bad = ocr.read_page(base + "report.png", { language: "eng" })
    if error then
        check("an unknown option is refused BY NAME",
              contains(error.message, "unknown option 'language'"), true)
        error.clear()
    else
        check("an unknown option is refused BY NAME", "accepted", "refused")
    end if

    ' A LANGUAGE NOBODY INSTALLED IS THE FAILURE THAT SAYS NOTHING: reading
    ' Korean with English data returns words at 30-45% confidence, not an error
    ' and not an empty page. Measured; installing the language took the same
    ' pages to 8-11x the usable words.
    on error goto next
    bad2 = ocr.read_page(base + "report.png", { languages: "zzz" })
    if error then
        check("an absent language is refused BY NAME",
              contains(error.message, "'zzz' is not installed"), true)
        error.clear()
    else
        check("an absent language is refused BY NAME", "accepted", "refused")
    end if

    ' THE CONTROL: a language that IS installed must work, or "refuses an absent
    ' language" is satisfied by refusing every language there is.
    ok2 = ocr.read_page(base + "report.png", { languages: "eng" })
    check("CONTROL: an installed language still reads", ok2.ok, true)

    print ""
    print "checks: " + string(G.checks)
    print "mismatches: " + string(G.bad)
end program

function check(label, got, want)
    G.checks = G.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        G.bad = G.bad + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
end function

' Is there ONE line carrying both? That is the question the whole design turns
' on -- not whether both appear somewhere in the text.
function _rowhas(g, a, b)
    for each ln in split(g, chr(10))
        if contains(ln, a) and contains(ln, b) then
            return true
        end if
    end for
    return false
end function

function _small(x, lim)
    if is_unknown(x) then
        return false
    end if
    if x < 0 then
        x = 0 - x
    end if
    return x <= lim
end function
