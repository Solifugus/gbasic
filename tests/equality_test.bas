' PLAT-EQ semantics fixture. Golden-compared by tests/run_equality.sh.
'
' `=` on two compound values used to be unconditionally TRUE. eval_comparison
' had no branch for arrays or records, so both sides fell through to a numeric
' coercion where a record becomes 0 and an array becomes 0 -- and 0 = 0. So
' {x:1} = {y:2}, [1,2] = [3,4,5] and even [] = [1,2,3] all answered true, while
' the ordering operators silently answered a comparison between two zeros.
'
' The damage was not in the operator, which few programs use on a record; it was
' in `contains` and `find`, which route through the same comparison. A search
' through an array of records matched element 0 and reported success, so
' contains(people, {name:"zed"}) was true for a name that was not there. An
' array of records is how every frame in the xlsx pipeline is represented.
'
' EVERY CHECK BELOW STATES ITS OWN EXPECTED ANSWER, and prints `ok` or a
' MISMATCH naming both sides. A plain golden cannot be trusted for this defect:
' a golden records whatever the binary answers as the expected output, so it
' would have happily enshrined `true` for {x:1} = {y:2}. The golden here pins
' that every check SAYS ok; the checks themselves are what decide correctness.

' --- an independent deep equality, written in gBASIC ---------------------------
' Deliberately NOT the same code path as `=`. The operator now delegates to
' value_storage_equal in C; this walks the structure in the language itself,
' using type/count/keys/has, and bottoms out on `=` only for scalars -- which
' were never broken. Where the two disagree, one of them is wrong, and the
' checks below assert they agree on every pair.
function deep_equal(a, b)
  if type(a) != type(b) then
    return false
  end if
  if is_array(a) then
    if count(a) != count(b) then
      return false
    end if
    i = 0
    while i < count(a)
      same = deep_equal(a[i], b[i])
      if same = false then
        return false
      end if
      i = i + 1
    end while
    return true
  end if
  if is_record(a) then
    ka = keys(a)
    kb = keys(b)
    if count(ka) != count(kb) then
      return false
    end if
    i = 0
    while i < count(ka)
      k = ka[i]
      present = has(b, k)
      if present = false then
        return false
      end if
      same = deep_equal(a[k], b[k])
      if same = false then
        return false
      end if
      i = i + 1
    end while
    return true
  end if
  return a = b
end function

' Checks one pair two ways: the operator's answer must be `want`, AND the
' independent walk must agree with the operator. A regression to the coercion
' fails the first; a wrong deep-comparison in C that the fixture author also got
' wrong in the expectation still fails the second.
function pair(label, a, b, want)
  got = a = b
  oracle = deep_equal(a, b)
  if got != want then
    return label + " MISMATCH got " + string(got) + " want " + string(want)
  end if
  if oracle != got then
    return label + " ORACLE DISAGREES operator " + string(got) + " walk " + string(oracle)
  end if
  return label + " ok"
end function

function chk(label, got, want)
  if got = want then
    return label + " ok"
  end if
  return label + " MISMATCH got " + string(got) + " want " + string(want)
end function

program main(args)
  print("-- the cases that used to be unconditionally true --")
  print(pair("differing records", { x: 1 }, { y: 2 }, false))
  print(pair("differing arrays", [1, 2], [3, 4, 5], false))
  print(pair("empty vs full", [], [1, 2, 3], false))
  print(pair("record vs array", { x: 1 }, [1], false))
  print(pair("array vs string", [1, 2], "hi", false))
  print(pair("record vs string", { x: 1 }, "hi", false))
  print(pair("empty record vs empty array", { }, [], false))

  print("-- equal compounds --")
  print(pair("identical records", { a: 1, b: "x" }, { a: 1, b: "x" }, true))
  print(pair("identical arrays", [1, 2, 3], [1, 2, 3], true))
  print(pair("empty records", { }, { }, true))
  print(pair("empty arrays", [], [], true))

  ' A record is a mapping, so the order fields were written in is not part of
  ' its meaning. value_storage_equal looks each field up BY NAME rather than
  ' walking both in parallel, which is what makes this true.
  print(pair("field order irrelevant", { a: 1, b: 2 }, { b: 2, a: 1 }, true))

  print("-- inequality is detected, not just equality --")
  print(pair("differing value", { a: 1 }, { a: 2 }, false))
  print(pair("differing key name", { a: 1 }, { b: 1 }, false))
  print(pair("extra field", { a: 1 }, { a: 1, b: 2 }, false))
  print(pair("missing field", { a: 1, b: 2 }, { a: 1 }, false))
  print(pair("differing element", [1, 2, 3], [1, 2, 4], false))
  print(pair("differing length", [1, 2], [1, 2, 3], false))
  print(pair("element order matters", [1, 2], [2, 1], false))

  print("-- deep, both directions --")
  print(pair("nested equal", [{ a: [1, 2] }], [{ a: [1, 2] }], true))
  print(pair("nested differing", [{ a: [1, 2] }], [{ a: [1, 3] }], false))
  print(pair("record of arrays equal", { xs: [1, 2], ys: [3] }, { ys: [3], xs: [1, 2] }, true))
  print(pair("record of arrays differing", { xs: [1, 2] }, { xs: [1, 9] }, false))
  print(pair("deeply nested equal", { a: { b: { c: [1, { d: 2 }] } } }, { a: { b: { c: [1, { d: 2 }] } } }, true))
  print(pair("deeply nested differing", { a: { b: { c: [1, { d: 2 }] } } }, { a: { b: { c: [1, { d: 3 }] } } }, false))

  print("-- mixed element types --")
  print(pair("mixed equal", [1, "two", true], [1, "two", true], true))
  print(pair("mixed differing", [1, "two", true], [1, "two", false], false))
  ' A number and a numeric string are different values, and the old coercion
  ' could not tell them apart at all.
  print(pair("number vs numeric string", [1], ["1"], false))

  print("-- scalars are unchanged --")
  print(pair("equal strings", "abc", "abc", true))
  print(pair("differing strings", "abc", "abd", false))
  print(pair("equal numbers", 3, 3, true))
  print(pair("int vs float form", 3, 3.0, true))
  print(pair("differing numbers", 3, 4, false))
  print(pair("equal booleans", true, true, true))
  print(pair("differing booleans", true, false, false))

  print("-- != is the negation, not a second implementation --")
  print(chk("!= differing records", { x: 1 } != { y: 2 }, true))
  print(chk("!= identical records", { a: 1 } != { a: 1 }, false))
  print(chk("!= differing arrays", [1] != [2], true))
  print(chk("!= identical arrays", [1, 2] != [1, 2], false))

  print("-- aliases and copies --")
  ' An array assigned to another name shares one backing store, and
  ' value_storage_equal takes an O(1) identity fast path for that case. It must
  ' reach the same answer as the elementwise walk.
  orig = [1, 2, 3]
  alias = orig
  print(pair("alias equals original", orig, alias, true))
  copy_of = [1, 2, 3]
  print(pair("independent copy equals", orig, copy_of, true))
  alias[0] = 99
  print(chk("mutating the alias detaches", orig = alias, false))
  print(chk("original unchanged", orig[0], 1))

  print("-- the damaging consequence: searching an array of records --")
  people = [{ name: "ann", age: 30 }, { name: "bob", age: 40 }]
  ' Every one of these returned the WRONG answer before: contains was true for
  ' anything at all, and find always returned 0.
  print(chk("contains present", contains(people, { name: "bob", age: 40 }), true))
  print(chk("contains absent", contains(people, { name: "zed", age: 1 }), false))
  print(chk("contains near-miss", contains(people, { name: "bob", age: 41 }), false))
  print(chk("contains partial record", contains(people, { name: "bob" }), false))
  print(chk("find present", find(people, { name: "bob", age: 40 }), 1))
  print(chk("find first", find(people, { name: "ann", age: 30 }), 0))
  ' `find` answers `nothing` for a miss, not -1 -- so a caller must test with
  ' is_nothing rather than comparing to an index. Worth pinning: before the fix
  ' a miss was indistinguishable from a hit at position 0.
  print(chk("find absent is nothing", is_nothing(find(people, { name: "zed", age: 1 })), true))
  print(chk("find present is not nothing", is_nothing(find(people, { name: "bob", age: 40 })), false))

  nested = [[1, 2], [3, 4]]
  print(chk("contains nested array", contains(nested, [3, 4]), true))
  print(chk("contains absent nested", contains(nested, [9, 9]), false))
  print(chk("find nested", find(nested, [3, 4]), 1))

  print("-- scalar searching still works --")
  print(chk("contains number", contains([1, 2, 3], 2), true))
  print(chk("contains absent number", contains([1, 2, 3], 9), false))
  print(chk("contains string", contains(["a", "b"], "b"), true))
  print(chk("find number", find([1, 2, 3], 3), 2))

  print("-- remove_value, which shares the comparison --")
  rv = remove_value([{ a: 1 }, { a: 2 }, { a: 3 }], { a: 2 })
  print(chk("remove_value removed one", count(rv), 2))
  print(chk("remove_value kept the first", rv[0].a, 1))
  print(chk("remove_value kept the last", rv[1].a, 3))
end program
