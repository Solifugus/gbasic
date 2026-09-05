' PLAT-NUL -- a gBASIC string is a counted sequence of bytes, and NUL is
' content. docs/reference.md says so in as many words:
'
'   "A gBASIC string is a binary-safe sequence of bytes ... any byte --
'    including NUL (chr(0)) -- is valid content; strings are not
'    NUL-terminated from the program's point of view."
'
' It was not true. `len`, `mid` and `contains` were length-aware while
' `replace`, `trim`, `join`, `split`, `repeat`, `starts_with` and `ends_with`
' read their arguments as C strings, and NOTHING SAID WHICH WAS WHICH. That is
' the reason this fixture enumerates the WHOLE family rather than the
' functions that were broken: the defect was not any one of them, it was that
' binary-safety was a per-function fact a caller had to discover one probe at
' a time.
'
' SELF-CHECKING RATHER THAN GOLDEN, and here that is forced. Every one of these
' defects returned a SHORTER PLAUSIBLE STRING or a WRONG BOOLEAN, never an
' error -- a golden would have recorded `1` as the length of a three-byte
' string and defended it.
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

' Three bytes: 'a', NUL, 'b'. Every length below is 3 unless stated.
s = "a" + chr(0) + "b"

' --- TIER: the subject carries an interior NUL --------------------------
' These were the length-aware ones all along. They are asserted anyway, and
' that is not padding: without them a regression that made `len` C-string
' would leave every "want 3" below agreeing with a wrong answer, and the
' whole file would pass while measuring nothing.
check("len", len(s), 3)
check("byte_count", byte_count(s), 3)
check("upper", len(upper(s)), 3)
check("lower", len(lower(s)), 3)
check("reverse", len(reverse(s)), 3)
check("concatenation", len(s + s), 6)
check("string()", len(string(s)), 3)
check("mid", len(mid(s, 0, 3)), 3)
check("left", len(left(s, 3)), 3)
check("right", len(right(s, 3)), 3)
check("contains", contains(s, "b"), true)
check("find", find(s, "b"), 2)
check("byte_at reads the NUL", byte_at(s, 1), 0)
check("hex_encode", hex_encode(s), "610062")

' These were not. Each returned a shorter value or a wrong boolean, silently.
check("replace, matching before the NUL", len(replace(s, "a", "A")), 3)
check("replace, matching after the NUL", len(replace(s, "b", "B")), 3)
check("trim", len(trim(s)), 3)
check("repeat", len(repeat(s, 2)), 6)
check("join", len(join([s, s], "-")), 7)
check("split, separator before the NUL", count(split(s, "a")), 2)
check("split, separator after the NUL", count(split(s, "b")), 2)
check("ends_with", ends_with(s, "b"), true)
check("starts_with", starts_with(s, "a"), true)

' --- TIER: the NEEDLE carries a NUL -------------------------------------
' The half gdash's report marked safe and which is not. A needle read as a C
' string compares only its head, or -- if it BEGINS with NUL -- compares
' nothing at all and matches everything. Both answered TRUE for text that does
' not match, which is the dangerous direction for a predicate used to
' validate input.
check("starts_with with a NUL needle is not a wildcard",
      starts_with("xy", chr(0)), false)
check("  nor is ends_with", ends_with("xy", chr(0)), false)
check("a needle's bytes past its NUL still count",
      starts_with("axy", "a" + chr(0) + "b"), false)
check("  and a genuine prefix still matches",
      starts_with(s, "a" + chr(0)), true)
check("  as does a genuine suffix", ends_with(s, chr(0) + "b"), true)
check("contains a NUL", contains(s, chr(0)), true)
check("find a NUL", find(s, chr(0)), 1)

' A NUL needle is an ORDINARY needle. Reading it as empty was the same
' C-string mistake one level along.
check("replace can rewrite a NUL", len(replace(s, chr(0), "XY")), 4)
check("  and the result no longer holds one", contains(replace(s, chr(0), "XY"), chr(0)), false)
check("split can separate on a NUL", count(split(s, chr(0))), 2)
check("  giving the bytes either side", first(split(s, chr(0))), "a")

' --- TIER: the refusals that must SURVIVE -------------------------------
' The control for the tier above. `empty` means LENGTH ZERO, and an empty
' needle still has no first occurrence to replace -- if the fix had simply
' deleted these checks, every case above would still pass.
on error goto next
x = replace(s, "", "X")
check("an EMPTY needle is still refused",
      contains(error.message, "cannot be empty"), true)
error.clear()
x = split(s, "")
check("  and so is an empty separator",
      contains(error.message, "cannot be empty"), true)
error.clear()
on error stop

' --- TIER: a round trip through bytes -----------------------------------
' An independent check on the whole family: build a subject from raw bytes,
' put it through the operations, and read the bytes back out. hex_encode is
' length-aware and was never broken, so it is an oracle rather than a second
' call into the code under test.
raw = from_bytes([0, 65, 0, 66, 0])
check("a subject that is mostly NUL survives", hex_encode(raw), "0041004200")
check("  through replace, which rewrites only the A", hex_encode(replace(raw, "A", "Z")), "005a004200")
check("  through join", hex_encode(join([raw, raw], "")), "00410042000041004200")
check("  through repeat", hex_encode(repeat(raw, 2)), "00410042000041004200")
check("  and trim leaves it alone", hex_encode(trim(raw)), "0041004200")

' --- TIER: multibyte text is unharmed -----------------------------------
' The NUL fix moved seven operations from strlen/strstr to the string header's
' length. That is a change to how far they read, and the question it raises is
' whether they still respect CODEPOINTS. They do, and not by luck: UTF-8 is
' self-synchronizing, so a valid multi-byte needle can only ever match at a
' codepoint boundary. This tier is what makes that a checked property rather
' than an argument -- and it belongs beside the NUL cases, because the two
' guarantees are about the same code reading the same bytes.
c = "café"
j = "日本語"
check("len counts codepoints", len(c), 4)
check("  while byte_count counts bytes", byte_count(c), 5)
check("replace a multibyte needle", replace(c, "é", "e"), "cafe")
check("  and a multibyte replacement", replace(j, "本", "X"), "日X語")
check("split on a multibyte separator", count(split(j, "本")), 2)
check("  and the parts are whole characters", first(split(j, "本")), "日")
check("join with a multibyte separator", join(["日", "語"], "・"), "日・語")
check("repeat a multibyte character", repeat("é", 3), "ééé")
check("  three codepoints", len(repeat("é", 3)), 3)
check("  and six bytes", byte_count(repeat("é", 3)), 6)
check("trim leaves multibyte content alone", trim("  日本語  "), j)
check("  and does NOT treat U+00A0 as padding",
      byte_count(trim(from_bytes([194, 160]) + "x")), 3)
check("starts_with a multibyte prefix", starts_with(j, "日"), true)
check("ends_with a multibyte suffix", ends_with(j, "語"), true)
check("mid still slices by codepoint", mid(j, 1, 1), "本")

' NUL AND MULTIBYTE TOGETHER, which is the combination neither guarantee covers
' on its own: three codepoints, seven bytes.
m = "日" + chr(0) + "語"
check("a mixed subject is 3 codepoints", len(m), 3)
check("  and 7 bytes", byte_count(m), 7)
check("  survives replace", byte_count(replace(m, "語", "本")), 7)
check("  survives trim", byte_count(trim(" " + m + " ")), 7)
check("  survives repeat", byte_count(repeat(m, 2)), 14)
check("  survives join", byte_count(join([m, m], "-")), 15)
check("  and ends_with sees past the NUL", ends_with(m, "語"), true)
check("  and split separates on it", first(split(m, chr(0))), "日")

' --- TIER: matching is by BYTE, and the reference says so ----------------
' Pinned rather than fixed. A needle that is NOT valid UTF-8 can match in the
' middle of a character, and then `find`'s codepoint index does not name a
' boundary and `replace` can produce invalid UTF-8 from valid input. This is
' byte semantics doing what byte semantics does, it is UNCHANGED by the NUL
' work (verified byte-identical against the pre-change binary), and it is now
' documented -- so it is pinned here to keep the documentation honest, not
' because the behaviour is desirable.
check("an invalid fragment matches mid-codepoint",
      starts_with(j, from_bytes([230])), true)
check("  find answers with an index that is not a boundary",
      find(j, from_bytes([156])), 2)
check("  and replace can produce invalid UTF-8 from valid input",
      byte_count(replace(j, from_bytes([156]), "!")), 9)
' THE CONTROL, and the reason the three above are acceptable: a needle that IS
' valid UTF-8 can never do this, whatever it is made of.
check("a VALID needle never matches mid-codepoint",
      replace(j, "本", "本"), j)
check("  nor does a valid prefix of the wrong character",
      starts_with(j, "本"), false)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
