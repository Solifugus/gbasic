' Headless driver for persist's read path (was Studio's STU-STORE).
'
' persist (then studio_store) used to PRE-VALIDATE every file with a scanner
' before handing it to `decode`, because `decode` raises and gBASIC cannot catch a
' raise. That scanner was quadratic (`mid` is O(i) on codepoint-indexed strings),
' so reading a 116 KB store took 92 s. PLAT-JSON's `try_decode` removes the need
' for it entirely.
'
' Dispatches on args[0]; args[1] is a throwaway directory, never printed.

function show(label, st)
  line = label + " status=" + st.status
  if st.status = "loaded" then
    ' `unknown` is part of the dialect the parser accepts but has no JSON form.
    if json_encodable(st.value) then
      line = line + " value=" + json_encode(st.value)
    else
      line = line + " value=(holds a value JSON cannot express)"
    end if
  end if
  if st.message != "" then
    line = line + " message=<" + st.message + ">"
  end if
  print line
  return nothing
end function

function put(path, text)
  f{file} = path
  write(f, text)
  return nothing
end function

program main(args)
  load persist from "../stdlib/persist.bas"

  mode = ""
  if count(args) > 0 then
    mode = args[0]
  end if
  dir = "/tmp/gbasic_stu_store"
  if count(args) > 1 then
    dir = args[1]
  end if
  persist.ensure_dir(dir)

  ' ---- the three states, unchanged, plus a reason for the bad one -------
  if mode = "status" then
    print "-- missing"
    show("no file        ", persist.read_status(dir + "/absent.json"))

    print "-- loaded"
    persist.write_atomic(dir + "/good.json", { schema_version: 1, name: "ws", n: 42 })
    show("written by us  ", persist.read_status(dir + "/good.json"))

    print "-- corrupt: every way a store file can be broken"
    put(dir + "/c1.json", "{ this is not json ]")
    show("garbage        ", persist.read_status(dir + "/c1.json"))
    put(dir + "/c2.json", "{\"a\":1")
    show("truncated      ", persist.read_status(dir + "/c2.json"))
    put(dir + "/c3.json", "")
    show("empty file     ", persist.read_status(dir + "/c3.json"))
    put(dir + "/c4.json", "{\"a\":\"unterminated}")
    show("unterminated   ", persist.read_status(dir + "/c4.json"))
    put(dir + "/c5.json", "{\"a\":1}trailing")
    show("trailing text  ", persist.read_status(dir + "/c5.json"))

    ' A corrupt file must still be readable AS BYTES afterwards -- Studio's
    ' recovery policy keeps the file rather than deleting it, so nothing here may
    ' consume or truncate it.
    f{file} = dir + "/c1.json"
    print "corrupt file still on disk bytes=" + file_size(f)
  end if

  ' ---- the dialect a store file may legitimately contain ---------------
  if mode = "dialect" then
    ' Studio always WRITES strict JSON (json_encode), but the reader now accepts
    ' exactly what `decode` accepts -- the historical gBASIC dialect included.
    ' Before PLAT-JSON the reader validated STRICTLY and decoded LENIENTLY, so it
    ' rejected files its own decoder could have read. These three are the whole
    ' difference, and they are reachable only by hand-editing a store.
    print "-- gBASIC dialect (hand-edited store)"
    put(dir + "/d1.json", "{\"a\":nothing}")
    show("bare nothing   ", persist.read_status(dir + "/d1.json"))
    put(dir + "/d2.json", "{\"a\":unknown}")
    show("bare unknown   ", persist.read_status(dir + "/d2.json"))
    put(dir + "/d3.json", "{\"a\":+1}")
    show("leading plus   ", persist.read_status(dir + "/d3.json"))

    ' What persist itself writes is strict JSON, and stays that way: write_atomic
    ' pre-checks json_encodable, so a dialect-only value never reaches a store
    ' through persist's own writer.
    persist.write_atomic(dir + "/d4.json", { a: nothing, b: 1 })
    fr{file} = dir + "/d4.json"
    print "we write      " + read(fr)
    print "refuses unknown=" + (not json_encodable({ a: unknown_free() }))
  end if

  ' ---- round-trip: what we write is what we read back ------------------
  if mode = "roundtrip" then
    samples = [
      { schema_version: 1, active_workspace: "ws-1", next_ws: 2 },
      { schema_version: 1, id: "ws-1", name: "member analytics",
        projects: [{ id: "proj-1", name: "A", path: "/p/a" }],
        nav: { selected_path: "/p/a/main.bas", expanded: ["/p/a/src"] } },
      { schema_version: 1, window: { width: 1024, height: 768, maximized: true },
        recent_files: ["/a.bas", "/b.bas"] },
      { schema_version: 1, unicode: "héllo → wörld ✓", escaped: "q\" b\\ n\n t\t" },
      { schema_version: 1, deep: { a: { b: { c: { d: [1, 2, { e: "f" }] } } } } }
    ]
    i = 0
    for each s in samples
      p = dir + "/rt" + i + ".json"
      persist.write_atomic(p, s)
      st = persist.read_status(p)
      same = false
      if st.status = "loaded" then
        same = (json_encode(st.value) = json_encode(s))
      end if
      print "sample " + i + " status=" + st.status + " identical=" + same
      i = i + 1
    end for
  end if

  ' ---- the point of the exercise: a large store opens quickly ----------
  if mode = "speed" then
    ' A results index at realistic worst case -- a document with 12 sections at
    ' full retention -- is about 115 KB. The pure-gBASIC validator took 92 s on
    ' exactly this shape; a bound of 5 s is far above what the C parser needs and
    ' far below what the validator could ever have managed, so this assertion is
    ' stable without being a timing race.
    recs = []
    s = 1
    while s <= 12
      r = 0
      while r < 20
        recs = append(recs, {
          result_id: "res-" + count(recs), section_id: "sec-" + s,
          section_fingerprint: "771125435:882236546",
          section_kind: "statements", section_name: nothing,
          started_epoch: 1785289530 + r, finished_epoch: 1785289531 + r,
          duration_seconds: 1, outcome: "finished", exit_code: 0, signal: 0,
          success: true, reason: "", message: "", split_out: "marked",
          split_err: "unavailable", split_reason: "",
          captures: { out_prefix: 4, out_target: 2, err_prefix: 0, err_target: 0 },
          truncated: [], attribution: [], run_seq: r + 1
        })
        r = r + 1
      end while
      s = s + 1
    end while
    p = dir + "/big.json"
    persist.write_atomic(p, { schema_version: 1, doc_path: "/proj/big.bas",
                                   next_result: count(recs) + 1, results: recs })
    f{file} = p
    print "index_bytes_over_100k=" + (file_size(f) > 100000)
    print "records=" + count(recs)

    t0 = epoch()
    st = persist.read_status(p)
    elapsed = epoch() - t0
    print "status=" + st.status + " restored=" + count(st.value.results)
    print "open_under_5s=" + (elapsed < 5)
  end if
end program

' `unknown` cannot be written as a bare literal into a record field here without
' the parser folding it, so it is produced through a function.
function unknown_free()
  return unknown
end function
