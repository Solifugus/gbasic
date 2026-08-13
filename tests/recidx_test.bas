' PLAT-RECIDX semantics fixture. Golden-compared by tests/run_recidx.sh.
'
' Records gained a lazily-built hash index from field name to slot
' (RECORD_INDEX_MIN_FIELDS in src/eval.c). Nothing a program can observe was
' meant to change, so this file is built around one idea: every check is run
' TWICE, once on a record small enough to still take the linear walk and once on
' a record large enough to be indexed. The linear walk is the oracle for the
' indexed path.
'
' Two things follow from that, and both are deliberate:
'
'   * Every check states its own EXPECTED value, computed from n rather than
'     written out, and prints "ok" or a MISMATCH naming both sides. A golden
'     alone would pin whatever the index does, including a wrong answer.
'   * Because every line is then "<label> <name> ok", the two batteries emit
'     IDENTICAL text apart from the label, so the runner can diff them against
'     each other. That diff is the parity assertion: it fails if the indexed
'     path answers differently from the walk, whatever the golden says.
'
' The two sizes must straddle RECORD_INDEX_MIN_FIELDS. If that constant moves,
' move these.

function ok(name, actual, expected)
  if string(actual) = string(expected) then
    return name + " ok"
  end if
  return name + " MISMATCH got [" + string(actual) + "] want [" + string(expected) + "]"
end function

function build(n, prefix)
  r = { }
  i = 0
  while i < n
    r[prefix + string(i)] = i * 10
    i = i + 1
  end while
  return r
end function

' Read every key back and sum. A name resolved to the wrong slot changes this.
function sum_all(r, n, prefix)
  total = 0
  i = 0
  while i < n
    total = total + r[prefix + string(i)]
    i = i + 1
  end while
  return total
end function

function battery(label, n)
  prefix = "key"
  r = build(n, prefix)
  sum = n * (n - 1) * 5

  print(label + " " + ok("count", count(r), n))
  print(label + " " + ok("sum", sum_all(r, n, prefix), sum))

  ' insertion order is observable and must survive indexing
  k = keys(r)
  print(label + " " + ok("keys-len", count(k), n))
  print(label + " " + ok("keys-first", k[0], prefix + "0"))
  print(label + " " + ok("keys-last", k[n - 1], prefix + string(n - 1)))
  v = values(r)
  print(label + " " + ok("values-first", v[0], 0))
  print(label + " " + ok("values-last", v[n - 1], (n - 1) * 10))

  ' absent key: bracket yields unknown, has() is false
  print(label + " " + ok("absent", r["nope"], unknown))
  print(label + " " + ok("absent-has", has(r, "nope"), false))
  print(label + " " + ok("present-has", has(r, prefix + string(n - 1)), true))

  ' overwrite an existing field -- record_set's update path finds the slot
  ' through the same index it must not corrupt, and must not add a field
  r[prefix + "0"] = 777
  print(label + " " + ok("overwrite", r[prefix + "0"], 777))
  print(label + " " + ok("overwrite-count", count(r), n))

  ' append past the size the index was built for -- forces a rehash on the
  ' large case. Re-reading an OLD key afterwards is what catches a rehash that
  ' dropped entries.
  r["added"] = 1
  r["also"] = 2
  print(label + " " + ok("grown-count", count(r), n + 2))
  print(label + " " + ok("grown-old-key", r[prefix + "1"], 10))
  print(label + " " + ok("grown-new-key", r["added"], 1))

  ' copy-on-write: the copy must share neither the fields nor the index
  c = r
  c[prefix + "1"] = 888
  c["copy_only"] = 9
  print(label + " " + ok("cow-orig", r[prefix + "1"], 10))
  print(label + " " + ok("cow-copy", c[prefix + "1"], 888))
  print(label + " " + ok("cow-orig-new-field", has(r, "copy_only"), false))
  print(label + " " + ok("cow-copy-new-field", has(c, "copy_only"), true))

  ' remove_key builds a NEW record; the removed key goes, the rest resolve
  d = remove_key(r, prefix + "1")
  print(label + " " + ok("removed-count", count(d), n + 1))
  print(label + " " + ok("removed-gone", d[prefix + "1"], unknown))
  print(label + " " + ok("removed-kept", d[prefix + "0"], 777))
  print(label + " " + ok("removed-kept-late", d["added"], 1))

  ' re-adding a removed key
  d[prefix + "1"] = 55
  print(label + " " + ok("readded", d[prefix + "1"], 55))
  print(label + " " + ok("readded-count", count(d), n + 2))

  ' Two records built alike must render alike, two built with different keys
  ' must not. Compared through `encode` rather than `=` because `=` on two
  ' records returns true whatever they hold -- pre-existing, confirmed against
  ' an untouched binary (DOGFOOD 2026-08-12) -- so `=` cannot observe the
  ' difference this check exists for. `encode` walks slots in order, so it sees
  ' content and order both.
  same_a = encode(build(n, prefix))
  same_b = encode(build(n, prefix))
  other = encode(build(n, "other"))
  print(label + " " + ok("encode-same", same_a = same_b, true))
  print(label + " " + ok("encode-differs", same_a = other, false))

  ' encode/decode and the actor wire format are separate walks over the fields
  back = decode(encode(build(n, prefix)))
  print(label + " " + ok("roundtrip-count", count(back), n))
  print(label + " " + ok("roundtrip-sum", sum_all(back, n, prefix), sum))
  wire = deserialize(serialize(build(n, prefix)))
  print(label + " " + ok("wire-count", count(wire), n))
  print(label + " " + ok("wire-sum", sum_all(wire, n, prefix), sum))
end function

program main(args)
  ' --- the paired batteries: the parity assertion ---------------------------
  battery("linear", 8)
  battery("indexed", 64)

  ' --- duplicate names -------------------------------------------------------
  ' A record CAN hold the same name twice (`select 1 as x, 2 as x`, or JSON with
  ' a repeated key). A linear walk returns the FIRST match, so the index must
  ' too -- it inserts in slot order and never overwrites an entry. Checked on
  ' both sides of the threshold, since only the large one is indexed.
  dup_small = decode("{\"dup\":1,\"a\":0,\"dup\":2}")
  dup_large = decode("{\"dup\":1,\"a\":0,\"b\":0,\"c\":0,\"d\":0,\"e\":0,\"f\":0,\"g\":0,\"h\":0,\"i\":0,\"j\":0,\"k\":0,\"l\":0,\"m\":0,\"n\":0,\"o\":0,\"p\":0,\"q\":0,\"r\":0,\"dup\":2}")
  print(ok("dup-linear", dup_small["dup"], 1))
  print(ok("dup-indexed", dup_large["dup"], 1))
  print(ok("dup-linear-count", count(dup_small), 3))
  print(ok("dup-indexed-count", count(dup_large), 20))

  ' --- keys chosen to stress probing ----------------------------------------
  ' Long shared prefixes make every strcmp run to the tail, and keys that are
  ' prefixes of one another are the classic way a length-blind compare goes
  ' wrong.
  pad = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
  p = { }
  i = 0
  while i < 40
    p[pad + string(i)] = i
    p[pad + string(i) + "x"] = 1000 + i
    i = i + 1
  end while
  print(ok("probe-count", count(p), 80))
  print(ok("probe-plain", p[pad + "7"], 7))
  print(ok("probe-suffixed", p[pad + "7x"], 1007))

  ' --- non-ASCII keys --------------------------------------------------------
  u = { }
  i = 0
  while i < 30
    u["ключ" + string(i)] = i
    u["日本語" + string(i)] = 100 + i
    i = i + 1
  end while
  print(ok("unicode-count", count(u), 60))
  print(ok("unicode-cyrillic", u["ключ12"], 12))
  print(ok("unicode-cjk", u["日本語12"], 112))

  ' --- boundaries: empty and single-field records ---------------------------
  ' An empty record owns no field array at all, so this is the one shape where
  ' the header does not exist; a lookup must not go looking for it.
  e = { }
  print(ok("empty-count", count(e), 0))
  print(ok("empty-absent", e["x"], unknown))
  print(ok("empty-has", has(e, "x"), false))
  e["only"] = 1
  print(ok("single-count", count(e), 1))
  print(ok("single-read", e.only, 1))

  ' --- record literal above the threshold, read via dot ---------------------
  lit = { f00: 0, f01: 1, f02: 2, f03: 3, f04: 4, f05: 5, f06: 6, f07: 7, f08: 8, f09: 9, f10: 10, f11: 11, f12: 12, f13: 13, f14: 14, f15: 15, f16: 16, f17: 17, f18: 18, f19: 19 }
  print(ok("literal-count", count(lit), 20))
  print(ok("literal-first", lit.f00, 0))
  print(ok("literal-mid", lit.f17, 17))
  print(ok("literal-last", lit.f19, 19))

  ' --- nesting: an indexed record holding indexed records -------------------
  outer = { }
  i = 0
  while i < 20
    outer["r" + string(i)] = build(20, "in")
    i = i + 1
  end while
  print(ok("nested-count", count(outer), 20))
  print(ok("nested-inner-count", count(outer["r7"]), 20))
  print(ok("nested-inner-read", outer["r7"]["in7"], 70))

  ' --- for each over an indexed record --------------------------------------
  seen = 0
  for each k in keys(build(50, "z"))
    seen = seen + 1
  end for
  print(ok("foreach", seen, 50))
end program
