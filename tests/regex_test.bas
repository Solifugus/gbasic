' TEXT-0 — regex in the core (docs/text_design.md §3, decisions §13.D–G).
'
' The surface is a `regex` VALUE KIND plus overloads of verbs that already exist,
' not a parallel set of `re_*` builtins (that earlier design is recorded in
' §13.D and is not what this tests):
'
'   regex(p [, flags])          compile once, reuse
'   match(s, p) / match_all     match record(s)      <- new names
'   contains / replace / split  overloaded on a regex second argument
'
' Four properties are load-bearing and are asserted throughout rather than in one
' place, because getting any of them wrong is silent:
'
'   * A STRING PATTERN STILL MEANS A LITERAL. Every overload is checked in both
'     modes side by side on input where the two answers DIFFER, so an overload
'     that swallowed the literal case would show up as a changed golden rather
'     than as nothing.
'   * OFFSETS ARE CODEPOINTS, not bytes. The engine works in bytes internally,
'     but `find` already returns a codepoint index so that
'     `mid(s, find(s, x), n)` composes, and two conventions for the same idea in
'     one language is a trap. Every offset assertion below is fed back through
'     `mid` to prove it composes.
'     The record carries `length` rather than `end`: `end` is a reserved word and
'     cannot be a record field, and start+length is what `mid` actually takes.
'   * NO MATCH IS `unknown`, not an error and not an empty string. That is
'     gBASIC's NA policy: a miss propagates rather than raising.
'   * A COMPILED regex GIVES THE SAME ANSWERS AS A BARE STRING. Compilation is a
'     speed device and must never become a correctness one.

function show(m)
  if is_unknown(m) then
    return "unknown"
  end if
  return "[" + m.text + "] @" + m.start + "+" + m.length + " groups=" + count(m.groups)
end function

' Renders a groups list so that unknown (group did not participate) and ""
' (group matched the empty string) are visibly different — §13.F.
function show_groups(g)
  out = []
  for each one in g
    if is_unknown(one) then
      append(out, "<unknown>")
    else
      append(out, "<" + one + ">")
    end if
  end for
  return join(out, " ")
end function

program main(args)
  print "-- regex(): a compiled pattern is a value"
  digits = regex("[0-9]+")
  print "reusable  =" + contains("order 1500", digits)
  print "reusable  =" + contains("no numerals", digits)
  ' Compiling must not change any answer versus the bare string.
  print "same as string=" + (contains("order 1500", digits) = contains("order 1500", regex("[0-9]+")))

  print "-- contains: string + LITERAL (new; this used to raise)"
  print "substring =" + contains("hello", "ell")
  print "absent    =" + contains("hello", "xyz")
  ' The literal case must stay literal: these pattern metacharacters are matched
  ' as themselves, so a literal that was silently treated as a regex flips here.
  print "dot literal=" + contains("hello", "h.llo")
  print "dot regex  =" + contains("hello", regex("h.llo"))
  print "star literal=" + contains("abc", "b*")
  print "star regex  =" + contains("abc", regex("b*"))

  print "-- contains: the ARRAY case is untouched"
  print "member    =" + contains(["a", "b"], "a")
  print "non-member=" + contains(["a", "b"], "z")

  print "-- contains: flags"
  print "no flag   =" + contains("HELLO", regex("hello"))
  print "i flag    =" + contains("HELLO", regex("hello", "i"))
  ' `.` excludes newline unless "s"; ^/$ are string-anchors unless "m".
  print "dot nl    =" + contains("a" + "\n" + "b", regex("a.b"))
  print "s flag    =" + contains("a" + "\n" + "b", regex("a.b", "s"))
  print "m flag    =" + contains("first" + "\n" + "second", regex("^second$", "m"))
  print "anchored  =" + contains("abc", regex("^abc$"))
  print "alternation=" + contains("cat", regex("^(cat|dog)$"))

  print "-- match: the match record"
  m = match("order 1500 shipped", "[0-9]+")
  print "found     =" + show(m)
  print "composes  =" + mid("order 1500 shipped", m.start, m.length)
  print "miss      =" + show(match("nothing", "[0-9]+"))
  print "miss is unknown=" + is_unknown(match("nothing", "[0-9]+"))

  print "-- match: SCANS, it does not anchor (§13.E)"
  ' Python's re.match anchors at the start; ours is re.search. Reading it as
  ' anchored gives wrong answers with no error, so it is pinned here.
  print "not at start=" + show(match("xx 42", "[0-9]+"))
  print "anchor is opt-in=" + is_unknown(match("xx 42", "^[0-9]+"))

  print "-- match: flags as a trailing argument on a bare string pattern"
  print "no flag   =" + is_unknown(match("HELLO", "hello"))
  print "i flag    =" + show(match("HELLO", "hello", "i"))
  ' Compiling with the same flags must give the same answer as passing them.
  print "same as regex()=" + (match("HELLO", "hello", "i").text = match("HELLO", regex("hello", "i")).text)

  print "-- regex values compare by pattern and flags, not by identity"
  print "same      =" + (regex("[0-9]+") = regex("[0-9]+"))
  print "diff pat  =" + (regex("[0-9]+") = regex("[a-z]+"))
  print "flag order=" + (regex("x", "is") = regex("x", "si"))
  print "diff flags=" + (regex("x", "i") = regex("x"))

  print "-- match: offsets are CODEPOINTS, not bytes"
  ' "héllo" is 5 codepoints but 6 bytes: a byte offset would report 3 here and
  ' feeding it to mid would slice the wrong characters.
  s = "héllo world"
  m2 = match(s, "llo")
  print "start     =" + m2.start
  print "bytes were=" + byte_count("hé")
  print "composes  =" + mid(s, m2.start, m2.length)
  print "agrees with find=" + (match(s, "llo").start = find(s, "llo"))

  print "-- match: capture groups"
  g = match("balance: $1,500.00 due", "\\$([0-9,]+)\\.([0-9]{2})")
  print "whole     =" + g.text
  print "groups    =" + join(g.groups, "|")
  print "count     =" + count(g.groups)

  print "-- match: groups is ALWAYS a list, empty when there are none"
  ng = match("order 1500", "[0-9]+")
  print "no groups =" + count(ng.groups)

  print "-- match: a non-participating group is unknown, not \"\" (§13.F)"
  ' Only one side of the alternation can match, so the other did not participate.
  ' An empty-matching group is a DIFFERENT thing and must render differently.
  alt = match("abc", "([0-9]+)|([a-z]+)")
  print "alternation=" + show_groups(alt.groups)
  emptyg = match("abc", "([0-9]*)abc")
  print "matched empty=" + show_groups(emptyg.groups)

  print "-- match_all"
  all = match_all("a1 b22 c333", "[0-9]+")
  print "count     =" + count(all)
  out = []
  for each one in all
    append(out, one.text + "@" + one.start)
  end for
  print "matches   =" + join(out, " ")
  print "none      =" + count(match_all("abc", "[0-9]+"))

  print "-- match_all: anchors restrict, they do not collapse to match"
  ' ^ and $ change WHERE a pattern may match, never how many results come back.
  print "unanchored=" + count(match_all("a1 b22 c333", "[0-9]+"))
  print "anchored  =" + count(match_all("a1 b22 c333", "^[0-9]+"))

  print "-- match_all: an empty-width match must terminate"
  ' `a*` matches the empty string at every position. A naive loop never advances.
  empties = match_all("bab", "a*")
  print "terminates=" + (count(empties) > 0)

  print "-- replace"
  print "literal   =" + replace("a1b2c3", "1", "#")
  print "regex     =" + replace("a1b2c3", regex("[0-9]"), "#")
  print "groups    =" + replace("2026-07-31", regex("([0-9]{4})-([0-9]{2})-([0-9]{2})"), "$3/$2/$1")
  print "no match  =" + replace("abc", regex("[0-9]"), "#")
  ' The literal path must not treat metacharacters as a pattern.
  print "dot literal=" + replace("a.c abc", ".", "#")
  print "dot regex  =" + replace("a.c abc", regex("."), "#")

  print "-- split"
  parts = split("a1b22c", regex("[0-9]+"))
  print "count     =" + count(parts) + " parts=" + join(parts, "|")
  print "no match  =" + join(split("abc", regex("[0-9]+")), "|")
  ' The literal path must not treat metacharacters as a pattern. The subject has
  ' NO literal dot, so the two modes must disagree: literal finds no separator
  ' and returns the whole string, regex splits at every character.
  print "dot literal=" + join(split("axbxc", "."), "|")
  print "dot regex  =" + join(split("axbxc", regex(".")), "|")

  print "-- find is NOT overloaded: it stays literal and returns a number"
  ' find's return shape (a single index) cannot carry a match record, which is
  ' why match exists as a separate name (§3). A literal find is unchanged.
  print "literal   =" + find("a1b2", "b")
  print "still number=" + (find("order 1500", "1500") = 6)

  print "-- binary safety: an interior NUL must not end the subject"
  ' gBASIC strings are binary-safe; a NUL-terminated engine would stop at byte 1
  ' and never see the B.
  nul = from_bytes([65, 0, 66])
  print "len       =" + byte_count(nul)
  print "finds B   =" + contains(nul, regex("B"))

  print "-- a compiled pattern is stable under reuse"
  ' Reusing one compiled value in a loop must give identical answers to compiling
  ' it fresh each time; compilation is a speed device, not a correctness one.
  same = true
  hot = regex("[0-9]+")
  i = 0
  while i < 50
    if not contains("order 1500", hot) then
      same = false
    end if
    if contains("order 1500", hot) != contains("order 1500", regex("[0-9]+")) then
      same = false
    end if
    i = i + 1
  end while
  print "stable over 50 uses=" + same

  ' Interleaving distinct compiled patterns must not let one disturb another —
  ' the failure mode the superseded single-entry cache would have had (§10).
  alt2 = true
  pa = regex("[a-z]+")
  pn = regex("[0-9]+")
  i = 0
  while i < 20
    if not contains("abc", pa) then
      alt2 = false
    end if
    if not contains("123", pn) then
      alt2 = false
    end if
    if contains("abc", pn) then
      alt2 = false
    end if
    if contains("123", pa) then
      alt2 = false
    end if
    i = i + 1
  end while
  print "stable when interleaved=" + alt2
end program
