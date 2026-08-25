' persist — crash-safe, versioned persistence for application state.
'
' Any program that remembers something across runs wants this: the write is
' atomic (temp file + rename), so a crash never leaves a half-written file, and
' the read reports missing/corrupt/loaded as a VALUE rather than raising.

program main(args)
  load persist from "../stdlib/persist.bas"

  home = "/tmp/gbasic_persist_example"
  persist.ensure_dir(home)

  persist.write_atomic(home + "/settings.json", { schema_version: 1, theme: "dark", recent: 10 })
  st = persist.read_status(home + "/settings.json")
  print "loaded status=" + st.status + " theme=" + st.value.theme + " recent=" + st.value.recent

  ' A file that is not there is not an error -- it is a state.
  missing = persist.read_status(home + "/absent.json")
  print "absent status=" + missing.status

  ' Neither is a corrupt one. The parser's reason comes back with it.
  bad{file} = home + "/broken.json"
  write(bad, "{ this is not json ]")
  corrupt = persist.read_status(home + "/broken.json")
  print "corrupt status=" + corrupt.status + " reported=" + (corrupt.message != "")

  ' Raw text, same atomic discipline, for artifacts that are not JSON.
  persist.write_text_atomic(home + "/notes.txt", "line one" + "\n")
  nf{file} = home + "/notes.txt"
  ' Bound first: `read(nf) = ...` inline is read as a modifier clause, because
  ' `read` is not in the builtin registry the clause lookahead consults.
  back = read(nf)
  print "text roundtrip=" + (back = "line one" + "\n")
end program
