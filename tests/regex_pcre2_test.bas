' REGULAR EXPRESSIONS ON PCRE2 (docs/regex_pcre2_design.md). Self-checking;
' run by tests/run_regex.sh.
'
' What the move added, what it changed, and what it must not have changed.
' Every new feature has a check that fails on the POSIX engine (it either
' refused the syntax or answered differently), and the controls are the
' answers that must stay as they were: ASCII classes, codepoint positions, a
' literal `$` in a replacement, `^` not matching mid-scan.

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

function texts(ms)
    out = []
    for each m in ms
        append(out, m.text)
    end for
    return out
end function

function replace_raised(subject, pattern, repl)
    on error goto handle
    r = replace(subject, regex(pattern), repl)
    return "ANSWERED " + r
handle:
    msg = error.message
    error.clear()
    return msg
end function

function raised(pattern, subject)
    on error goto handle
    m = match(subject, pattern)
    return "ANSWERED"
handle:
    msg = error.message
    error.clear()
    return msg
end function

print "-- WHAT STUDIO ASKED FOR"
check("\\b word boundary", texts(match_all("cat concat cat.", "\\bcat\\b")), ["cat", "cat"])
check("\\B inside a word", match("concat", "\\Bcat").start, 3)
check("lazy .*?", match("<a><b>", "<.*?>").text, "<a>")
check("  control: greedy .* takes the most", match("<a><b>", "<.*>").text, "<a><b>")
check("lookahead", match("price: 100 USD", "[0-9]+(?= USD)").text, "100")
check("negative lookahead", texts(match_all("a1 b2 a3", "a(?!1)[0-9]")), ["a3"])
check("lookbehind", match("cost $42", "(?<=\\$)[0-9]+").text, "42")
check("backreference in a pattern", match("the the cat", "\\b(\\w+) \\1\\b").text, "the the")
m = match("2026-11-03", "(?<year>[0-9]{4})-(?<month>[0-9]{2})-(?<day>[0-9]{2})")
check("named groups by name", [m.named.year, m.named.month, m.named.day], ["2026", "11", "03"])
check("  and still by position", m.groups, ["2026", "11", "03"])
check("a pattern without names has no `named`", has(match("ab", "(a)b"), "named"), false)
check("inline (?i)", match("HELLO", "(?i)hello").text, "HELLO")
check("[\\D] inside a bracket (POSIX refused it)", texts(match_all("a1b", "[\\D]")), ["a", "b"])

print ""
print "-- REPLACEMENT"
check("$1 and $2", replace("John Smith", regex("(\\w+) (\\w+)"), "$2, $1"), "Smith, John")
check("\\1 is the same reference", replace("John Smith", regex("(\\w+) (\\w+)"), "\\2 \\1"), "Smith John")
check("${name}", replace("2026-11-03", regex("(?<y>[0-9]+)-(?<m>[0-9]+)-(?<d>[0-9]+)"), "${d}/${m}/${y}"), "03/11/2026")
check("${10}, past nine", replace("abcdefghijk", regex("(a)(b)(c)(d)(e)(f)(g)(h)(i)(j)(k)"), "${10}${11}"), "jk")
check("$0 the whole match", replace("ab", regex("b"), "[$0]"), "a[b]")
check("$$ a literal dollar", replace("5", regex("[0-9]"), "$$$0"), "$5")
check("control: a $ before a non-reference stays literal", replace("x", regex("x"), "US$ and $"), "US$ and $")
check("$5 with no group 5 raises, naming the fix (it was silently empty)",
      replace_raised("x", "x", "$5.00"), "replace: $5 names group 5, and the pattern has 0; write $$ for a literal $")
check("  and $$5.00 is the price", replace("x", regex("x"), "$$5.00"), "$5.00")
check("  checked before matching: no match still raises", replace_raised("abc", "z", "$1"),
      "replace: $1 names group 1, and the pattern has 0; write $$ for a literal $")
check("control: \\ before a non-digit stays literal", replace("x", regex("x"), "C:\\temp"), "C:\\temp")
check("an unknown name raises", replace_raised("ab", "a", "[${nope}]"),
      "replace: ${nope} names a group the pattern does not have; write $$ for a literal $")
check("a group that did not take part is empty", replace("b", regex("(a)?b"), "<$1>"), "<>")

print ""
print "-- WHAT CHANGED: leftmost-first, not leftmost-longest"
check("a|ab on \"ab\" takes the first alternative", match("ab", "a|ab").text, "a")
check("  longest first gives the long one", match("ab", "ab|a").text, "ab")

print ""
print "-- A DEFECT THAT WENT WITH POSIX: no phantom group"
check("regex(\"a.b\", \"ms\") has no capture group", count(match("a" + chr(10) + "b", regex("a.b", "ms")).groups), 0)
check("regex(\"a[^x]b\", \"m\") has none either", count(match("ayb", regex("a[^x]b", "m")).groups), 0)

print ""
print "-- WHAT MUST NOT HAVE CHANGED"
check("\\d stays ASCII: an Arabic-Indic digit is not a digit", is_unknown(match(chr(1633), "\\d")), true)
check("\\w stays ASCII", is_unknown(match(chr(233), "\\w")), true)
check("positions are codepoints", match(chr(233) + chr(233) + "x", "x").start, 2)
check("  as mid reads them", mid(chr(233) + "ab", match(chr(233) + "ab", "b").start, 1), "b")
check("^ does not match mid-scan", texts(match_all("aaa", "^a")), ["a"])
check("  with \"m\" it matches at each line", texts(match_all("a" + chr(10) + "a", regex("^a", "m"))), ["a", "a"])
check("a lookbehind sees before the scan position", texts(match_all("abab", "(?<=a)b")), ["b", "b"])
check("a zero-width match still advances", count(match_all("abc", "x*")), 4)
check("split on a pattern", split("a1b22c", regex("[0-9]+")), ["a", "b", "c"])
check("caseless matches beyond ASCII", match(chr(201), regex(chr(233), "i")).text, chr(201))

print ""
print "-- BYTES"
check("a NUL in the pattern matches (POSIX refused it)",
      match("x" + chr(0) + "y", regex("x" + chr(0) + "y")).length, 3)
check("a NUL in the subject is content", contains(from_bytes([65, 0, 66]), regex("B")), true)
bad = from_bytes([97, 255, 98])
check("invalid UTF-8 in the subject does not stop a match", match(bad, "b").text, "b")
check("  and a match does not cross the bad byte", is_unknown(match(bad, "a.b")), true)

print ""
print "-- BACKTRACKING IS BOUNDED: it raises, it does not hang"
evil = repeat("a", 30) + "b"
msg = raised("(a+)+$", evil)
check("(a+)+$ on thirty a's and a b raises", contains(msg, "backtracks without end"), true)
check("  naming the usual cause", contains(msg, "(a+)+"), true)
check("control: the same pattern on a match answers", match(repeat("a", 30), "(a+)+$").length, 30)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
