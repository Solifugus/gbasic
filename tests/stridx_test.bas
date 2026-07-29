' PLAT-STRIDX correctness golden.
'
' This file asserts what string indexing MEANS, not how fast it is. It exists to
' be run against the interpreter both BEFORE and AFTER the indexing work: the
' output must be byte-identical either way. A moved line here is a semantic
' change, which is a defect -- the phase changes speed, not behaviour.
'
' Every access pattern the cache can get wrong is here: forward, backward,
' random, and alternating; one-byte and multibyte content; strings rebuilt
' between accesses; invalid UTF-8, where the lenient rule counts each malformed
' byte as its own unit; interior NULs, which no C string function would survive;
' and the empty and single-unit degenerate cases.

' Byte codes of a string, comma-separated -- safe to print for content that is
' not valid UTF-8 and would otherwise corrupt the golden.
function codes(s)
  out = ""
  i = 0
  n = byte_count(s)
  while i < n
    if i > 0 then
      out = out + ","
    end if
    out = out + byte_at(s, i)
    i = i + 1
  end while
  return out
end function

' A string's units listed by index, rendered as byte codes.
function units(s)
  out = ""
  i = 0
  n = len(s)
  while i < n
    if i > 0 then
      out = out + " "
    end if
    out = out + "[" + codes(mid(s, i, 1)) + "]"
    i = i + 1
  end while
  return out
end function

program main(args)
  ' ---- degenerate cases ------------------------------------------------
  print "-- empty"
  e = ""
  print "len=" + len(e) + " bytes=" + byte_count(e)
  print "mid=[" + codes(mid(e, 0, 1)) + "] left=[" + codes(left(e, 3)) + "] right=[" + codes(right(e, 3)) + "]"

  print "-- single unit"
  a = "A"
  m = chr(233)
  print "ascii len=" + len(a) + " bytes=" + byte_count(a) + " units=" + units(a)
  print "multi len=" + len(m) + " bytes=" + byte_count(m) + " units=" + units(m)

  ' ---- forward sequential scan -----------------------------------------
  ' The pattern the phase exists for. Mixed content so the codepoint index and
  ' the byte offset diverge from the second unit onward.
  print "-- forward scan, mixed"
  s = "aé→b" + chr(120) + chr(955) + "z"
  print "len=" + len(s) + " bytes=" + byte_count(s)
  print "units=" + units(s)

  ' ---- backward sequential scan ----------------------------------------
  print "-- backward scan, mixed"
  out = ""
  i = len(s) - 1
  while i >= 0
    out = out + "[" + codes(mid(s, i, 1)) + "]"
    i = i - 1
  end while
  print out

  ' ---- random access ---------------------------------------------------
  ' A fixed permutation, so the golden is stable and the order is not sequential.
  print "-- random access, mixed"
  order = [3, 0, 6, 1, 5, 2, 4]
  out = ""
  for each k in order
    out = out + k + ":[" + codes(mid(s, k, 1)) + "] "
  end for
  print trim(out)

  ' ---- alternating access ----------------------------------------------
  ' Front and back in turn -- the worst case for a single forward cursor.
  print "-- alternating access, mixed"
  out = ""
  lo = 0
  hi = len(s) - 1
  while lo <= hi
    out = out + "[" + codes(mid(s, lo, 1)) + "]"
    if lo != hi then
      out = out + "[" + codes(mid(s, hi, 1)) + "]"
    end if
    lo = lo + 1
    hi = hi - 1
  end while
  print out

  ' ---- pure ASCII, longer ----------------------------------------------
  ' Where codepoint index and byte offset coincide for the whole string.
  print "-- ascii block"
  ab = "the quick brown fox"
  print "len=" + len(ab) + " bytes=" + byte_count(ab)
  print "units=" + units(ab)
  print "mid(4,5)=" + mid(ab, 4, 5) + " left(3)=" + left(ab, 3) + " right(3)=" + right(ab, 3)

  ' ---- invalid UTF-8 ---------------------------------------------------
  ' Lenient rule: a malformed byte is one unit. 0xC3 0x28 is a truncated
  ' two-byte lead followed by a non-continuation; 0x80 is a stray continuation;
  ' 0xFF is never a legal UTF-8 byte at all.
  print "-- invalid utf-8"
  bad = from_bytes([65, 195, 40, 128, 255, 66])
  print "len=" + len(bad) + " bytes=" + byte_count(bad)
  print "units=" + units(bad)
  print "mid(1,3)=" + codes(mid(bad, 1, 3))
  print "left(2)=" + codes(left(bad, 2)) + " right(2)=" + codes(right(bad, 2))

  ' ---- interior NUL ----------------------------------------------------
  print "-- interior nul"
  nul = from_bytes([65, 0, 66, 0, 0, 67])
  print "len=" + len(nul) + " bytes=" + byte_count(nul)
  print "units=" + units(nul)
  print "mid(1,4)=" + codes(mid(nul, 1, 4))
  print "find B=" + find(nul, "B")

  ' ---- a NUL and a multibyte unit in the same string --------------------
  print "-- nul plus multibyte"
  both = from_bytes([0, 195, 169, 0, 226, 134, 146])
  print "len=" + len(both) + " bytes=" + byte_count(both)
  print "units=" + units(both)

  ' ---- mutation between accesses ---------------------------------------
  ' Any cached index must follow the VALUE, not the variable. Each step below
  ' reads the string (populating whatever cache exists), then rebinds it to
  ' different content, then reads again.
  print "-- mutation between accesses"
  v = "abc"
  print "1 len=" + len(v) + " u1=" + mid(v, 1, 1)
  v = v + chr(233) + "d"
  print "2 len=" + len(v) + " u3=" + codes(mid(v, 3, 1)) + " u4=" + mid(v, 4, 1)
  v = mid(v, 0, 2)
  print "3 len=" + len(v) + " units=" + units(v)
  v = "é" + v
  print "4 len=" + len(v) + " units=" + units(v)
  v = from_bytes([255, 65])
  print "5 len=" + len(v) + " units=" + units(v)
  v = ""
  print "6 len=" + len(v) + " empty=" + (v = "")

  ' Aliasing: two names for equal content, one rebound. The other must not move.
  print "-- aliasing"
  p = "héllo"
  q = p
  p = p + "!"
  print "p=" + p + " lenp=" + len(p) + " q=" + q + " lenq=" + len(q)

  ' ---- slice bounds and clamping ---------------------------------------
  print "-- clamping"
  c = "héllo"
  print "mid(0,99)=" + mid(c, 0, 99) + " mid(99,1)=[" + mid(c, 99, 1) + "]"
  print "mid(2,0)=[" + mid(c, 2, 0) + "] left(0)=[" + left(c, 0) + "] right(0)=[" + right(c, 0) + "]"
  print "left(99)=" + left(c, 99) + " right(99)=" + right(c, 99)
  print "mid(-1,2)=" + mid(c, 0 - 1, 2) + " left(-1)=[" + left(c, 0 - 1) + "]"

  ' ---- mid's four-argument replacement form ----------------------------
  print "-- mid replacement"
  print mid(c, 1, 2, "XY")
  print mid(c, 0, 1, "→")
  print codes(mid(nul, 1, 1, "Z"))

  ' ---- the other position-taking operations ----------------------------
  print "-- find / split / reverse / compare"
  f = "aé→bé→c"
  print "find(é)=" + find(f, "é") + " find(→)=" + find(f, "→") + " find(c)=" + find(f, "c")
  print "find(zz)=" + find(f, "zz")
  print "split=" + count(split(f, "é")) + " join=" + join(split(f, "é"), "|")
  print "reverse=" + reverse(f) + " len=" + len(reverse(f))
  print "upper=" + upper(f) + " lower=" + lower(upper(f))
  print "starts=" + starts_with(f, "aé") + " ends=" + ends_with(f, "→c")
  print "eq=" + (f = "aé→bé→c") + " lt=" + ("a" < "b") + " gt=" + ("é" > "a")

  ' ---- a long string, exercised at both ends ---------------------------
  ' Long enough that a full walk per access is visible, short enough that the
  ' golden stays readable. Content is multibyte so the cursor is really used.
  print "-- long string, ends and middle"
  big = ""
  i = 0
  while i < 500
    big = big + "aé→"
    i = i + 1
  end while
  print "len=" + len(big) + " bytes=" + byte_count(big)
  print "first=" + mid(big, 0, 3) + " last=" + mid(big, len(big) - 3, 3)
  print "mid=" + mid(big, 750, 3) + " at1000=" + mid(big, 1000, 1)
  print "left3=" + left(big, 3) + " right3=" + right(big, 3)
  ' Read the far end first, then the near end: a forward-only cursor must not
  ' report a stale answer when the index goes backwards.
  print "back-then-front=" + mid(big, 1400, 1) + mid(big, 2, 1) + mid(big, 1401, 1) + mid(big, 0, 1)

  ' ---- the same content read three ways, at a size that forces the index ----
  ' Long enough (>= 512 codepoints) that the sparse codepoint index is built, and
  ' multibyte so it is really used. A forward walk, a backward walk and a
  ' stride-7 walk must all report the same characters -- that is what proves the
  ' index agrees with the cursor and with a walk from zero. Summed by codepoint
  ' so the check is one comparable number rather than 1500 lines of golden.
  print "-- three traversals of one string agree"
  fwd = 0
  i = 0
  while i < len(big)
    fwd = fwd + code(mid(big, i, 1))
    i = i + 1
  end while
  bwd = 0
  i = len(big) - 1
  while i >= 0
    bwd = bwd + code(mid(big, i, 1))
    i = i - 1
  end while
  ' Stride 7 is coprime with the index stride, so the walk lands inside sampled
  ' blocks rather than only on their boundaries.
  strided = 0
  visits = 0
  i = 0
  while i < len(big)
    strided = strided + code(mid(big, i, 1))
    visits = visits + 1
    i = i + 7
  end while
  print "forward=" + fwd + " backward=" + bwd + " equal=" + (fwd = bwd)
  print "strided=" + strided + " visits=" + visits
end program
