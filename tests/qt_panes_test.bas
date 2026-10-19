' PANE SIZES (§0.22) -- `qt.pane(...).sizes` and `qt.resize_panes`, which is how
' "restore my layout" is written. Under QT_QPA_PLATFORM=offscreen, real
' QSplitters, no display.
'
' §0.19 LEFT THIS OPEN FOR TWO REASONS AND BOTH ARE STILL TRUE. `setSizes`
' takes a QList<int>, so measured, `sizes` is NOT a property (index -1) and
' `setSizes` is NOT invokable by name (-1) -- the by-name route that reaches
' everything else in this backend cannot see either. And a `split` CANNOT BE
' NAMED, its one slot being the direction, so there is nothing to address.
'
' SO IT IS ADDRESSED BY WALKING UP FROM A PART, which is the rule `qt.pane`
' already follows and needed no new spelling: `sizes` joins what `qt.pane`
' answers, and the write half is a verb taking the same part.
'
' STUDIO NEEDS IT. Its shell reads `split.position` for three splits out of the
' saved session and writes them back on exit -- so without this, a converted
' Studio forgets its layout every time, which is a feature it already has.
' GTK's `position` is ONE number because a Gtk.Paned has exactly two children;
' a QSplitter holds any number, so this is a LIST.
'
' THREE MEASUREMENTS SHAPED IT, and the first is why nothing here asserts an
' exact size. (1) `setSizes` IS NOT EXACT: asked for 260/540 of an 800-wide
' splitter it gave 252/522, the difference being the handle and the layout's
' margins -- so a round-trip on the value would fail on a correct
' implementation. (2) A WRONG COUNT FAILS SILENTLY: [100] on a two-pane
' splitter gave [774, 0], COLLAPSING the second pane, and [200,300,400] gave
' [310,464], honouring none of the three -- both perfectly ordinary-looking
' layouts that are not what was asked for, which is why a count mismatch is
' REFUSED here rather than passed to Qt. (3) A WINDOW RESIZE RE-PROPORTIONS
' (194/580 became 244/730), so what a program stores is a RATIO in effect, and
' that is what the checks below assert.
'
' SELF-CHECKING NOT GOLDEN AND FORCED: every defect here is a plausible layout.

function check(what, got, want)
    if string(got) = string(want) then
        print "ok   " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
end function

' STUDIO'S OWN SHELL SHAPE, which is what this is for: a browser beside an
' editor above a console, three splits, each with a remembered position.
window app( title: "Panes", width: 900, height: 500 )
    split across
        scroll browser
            column( margin: 4 )
                text f1( label: "one.bas" )
            end column
        end scroll
        split down
            source code( plainText: "x = 1" )
            table console to show rows
                field line( label: "Line" )
            end table
        end split
    end split
end window

' A WINDOW WITH NO SPLIT, for the refusal that has nothing to do with scroll
' areas: `qt.pane` already answers `unknown` for a part in neither region, and
' the writer must refuse rather than reach for whatever it can find.
window flat( title: "Flat", width: 300, height: 160 )
    column
        field only()
    end column
end window

' A THREE-PANE SPLIT, because `split` admits any number and a two-pane-only
' implementation would pass every check above.
window three( title: "Three", width: 900, height: 300 )
    split across
        field a()
        field b()
        field c()
    end split
end window

program main()
    rows = [{ line: "ready" }]
    qt.open(app)

    ' ---- the sizes are part of what a pane IS ------------------------------
    outer = qt.pane(app, "browser")
    check("the outer split has two panes", outer.panes, 2)
    check("and two sizes", count(outer.sizes), 2)
    ' THEY ADD UP TO THE SPLITTER, near enough: the handle and the margins are
    ' the difference, which is exactly why no check here is exact.
    total = outer.sizes[0] + outer.sizes[1]
    check("the sizes fill the split", total > 700 and total < 900, true)
    check("and both panes have room", outer.sizes[0] > 0 and outer.sizes[1] > 0,
          true)

    ' ---- setting them, asserted as a RATIO ---------------------------------
    ' NOT AS A VALUE: `setSizes` is not exact, so asking for 200/700 and
    ' checking for 200 would fail on a correct implementation. What a program
    ' actually wants back is the proportion, and that is what survives.
    qt.resize_panes(app, "browser", [200, 700])
    narrow = qt.pane(app, "browser").sizes
    check("the first pane is now the smaller", narrow[0] < narrow[1], true)
    check("and about a quarter of the width",
          narrow[0] * 4 > narrow[1] and narrow[0] * 2 < narrow[1], true)

    qt.resize_panes(app, "browser", [700, 200])
    wide = qt.pane(app, "browser").sizes
    check("and it can be made the larger", wide[0] > wide[1], true)
    ' THE DIFFERENCE IS THE ASSERTION: either reading alone passes on a verb
    ' that does nothing, since both are plausible layouts.
    check("so the two settings really differ", narrow[0] < wide[0], true)

    ' ---- the inner split is addressed by its own part ----------------------
    inner = qt.pane(app, "code")
    check("the inner split has two panes", inner.panes, 2)
    check("and its own sizes", count(inner.sizes), 2)
    qt.resize_panes(app, "code", [100, 300])
    ' AND SETTING THE INNER ONE LEAVES THE OUTER ALONE, which is the check that
    ' catches a walk that found the wrong splitter -- every size check above
    ' would still pass on one that resized whichever split it met first.
    after = qt.pane(app, "browser").sizes
    check("resizing the inner split left the outer alone", after[0], wide[0])
    small = qt.pane(app, "code").sizes
    check("and the inner one moved", small[0] < small[1], true)

    ' ---- a table as a direct pane is found, which is the control ----------
    ' Without it, "a part whose region is a scroll is refused" is satisfied by
    ' a walk that refuses everything that is not a plain widget.
    tb = qt.pane(app, "console")
    check("a table as a direct pane reports its split", tb.kind, "split")
    check("and its sizes", count(tb.sizes), 2)

    ' ---- a pane may be collapsed, and come back ---------------------------
    ' 0 IS LEGAL and is how a shell hides a panel without forgetting it.
    qt.resize_panes(app, "browser", [0, 900])
    check("a pane can be collapsed", qt.pane(app, "browser").sizes[0], 0)
    qt.resize_panes(app, "browser", [300, 600])
    check("and brought back", qt.pane(app, "browser").sizes[0] > 0, true)

    ' ---- refusals, each beside its nearest legal neighbour -----------------
    on error goto next
    ' THE LOAD-BEARING REFUSAL, and it is measured rather than cautious: one
    ' size on a two-pane split COLLAPSES the second (Qt answered [774, 0]) and
    ' three sizes honour NONE of them ([310, 464]) -- both ordinary-looking
    ' layouts that are not what was asked for.
    qt.resize_panes(app, "browser", [500])
    check("too few sizes is refused, naming both counts", error.message,
          "qt.resize_panes: that split has 2 panes and 1 size was given")
    error.clear()

    qt.resize_panes(app, "browser", [100, 200, 300])
    check("and too many", error.message,
          "qt.resize_panes: that split has 2 panes and 3 sizes were given")
    error.clear()

    qt.resize_panes(app, "f1", [10, 20])
    ' A PART WHOSE NEAREST REGION IS A SCROLL, which is the interesting case:
    ' `f1` sits inside a `scroll` that is itself a pane, and the walk stops at
    ' the nearest region -- the rule `qt.pane` already follows. Resizing "the
    ' split f1 is in" would otherwise reach past the thing that holds it.
    check("a part whose nearest region is a scroll is refused", error.message,
          "qt.resize_panes: `f1` is not in a split")
    error.clear()

    qt.resize_panes(app, "nope", [10, 20])
    check("an unknown part is refused", error.message,
          "qt.resize_panes: this window has no `nope`")
    error.clear()

    qt.resize_panes(app, "browser", [-10, 500])
    check("a negative size is refused rather than clamped", error.message,
          "qt.resize_panes: a pane cannot be -10 wide")
    error.clear()

    qt.resize_panes(app, "browser", [])
    check("an empty list is refused", error.message,
          "qt.resize_panes: that split has 2 panes and 0 sizes were given")
    error.clear()

    qt.resize_panes(app, "browser", ["wide", "narrow"])
    check("and a size that is not a number", error.message,
          "qt.resize_panes: a pane's size must be a number")
    error.clear()
    on error stop

    ' ---- a `scroll` reports no sizes, which is not the same as none --------
    sc = qt.pane(app, "f1")
    check("a scroll is a scroll", sc.kind, "scroll")
    check("and carries no sizes at all", has(sc, "sizes"), false)

    qt.close(app)

    ' ---- a window with no split at all -----------------------------------
    qt.open(flat)
    on error goto next
    qt.resize_panes(flat, "only", [10, 20])
    check("a part in a window with no split is refused", error.message,
          "qt.resize_panes: `only` is not in a split")
    error.clear()
    on error stop
    check("and qt.pane says it is in neither region",
          is_unknown(qt.pane(flat, "only")), true)
    qt.close(flat)

    ' ---- three panes, because two-pane-only would pass everything above ----
    qt.open(three)
    t = qt.pane(three, "b")
    check("a three-pane split says three", t.panes, 3)
    check("and gives three sizes", count(t.sizes), 3)
    qt.resize_panes(three, "b", [100, 300, 500])
    got = qt.pane(three, "b").sizes
    check("the three are in increasing order",
          got[0] < got[1] and got[1] < got[2], true)
    qt.close(three)
end program
