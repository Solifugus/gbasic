' ocr.grid on SYNTHETIC pages, so every position is exact and no engine is
' needed (book gaps G52 and G53). Every answer is written by hand.
load ocr

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

function w(text, left, top, line)
    return { text: text, left: left, top: top, width: len(text) * 10, height: 20,
             block: 1, paragraph: 1, line: line, conf: 90 }
end function

function refusal(page, options)
    on error goto next
    g = ocr.grid(page, options)
    e = error
    if e then
        return e.message
    end if
    return "laid out"
end function

' G52: "Way" starts at 145, inside "Industrial" (which ends at 150), so its
' column falls inside the line: it used to be appended with no space
p = { skew: 0, words: [ w("1200", 0, 0, 1), w("Industrial", 50, 0, 1), w("Way", 145, 0, 1),
                        w("Total", 0, 40, 2), w("9.40", 60, 40, 2) ] }
check("G52 words keep a space between them", ocr.grid(p), "1200 Industrial Way" + chr(10) + "Total 9.40")
gl = ocr.grid_lines(p)
check("G52 grid_lines gives the words placed on each line", count(gl[0].words), 3)
check("G52   with each word's index into page.words", gl[0].words[2].index, 2)
check("G52   and its column in the line", gl[0].words[2].column, 16)
check("G52   so a value names the words it is made of", gl[1].words[1].index, 4)
check("G52 grid and grid_lines agree on the text", gl[1].text, "Total 9.40")

' G53: the book's Ingram invoice, 2.2 degrees across 900 pixels drifts 35
' against a 37.5 line pitch, and its rows carried the next row's figures
long = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
ingram = { skew: 2.2, words: [ w("a", 0, 0, 1), w(long, 10, 0, 1), w("c", 0, 37.5, 2), w("d", 0, 75, 3) ] }
check("G53 a page whose drift passes half its pitch is refused below max_skew",
      contains(refusal(ingram, nothing), "drifts 35 pixels, more than half its 38-pixel line pitch"), true)
' the control: the same page level, and the same skew on a short span
level = ingram
level.skew = 0
check("G53   the same page level is laid out", refusal(level, nothing), "laid out")
narrow = { skew: 2.2, words: [ w("a", 0, 0, 1), w("bb", 10, 0, 1), w("c", 0, 37.5, 2), w("d", 0, 75, 3) ] }
check("G53   and the same skew across a short span is too", refusal(narrow, nothing), "laid out")
' max_skew is the caller's to set
check("G53 max_skew can be set", contains(refusal(narrow, { max_skew: 1 }), "past the 1 where rows stop"), true)
check("G53 an unknown option is refused", contains(refusal(narrow, { maxskew: 1 }), "unknown option 'maxskew'"), true)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
