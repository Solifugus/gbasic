' filetree — a directory as a navigable tree of plain values.
'
' The tree is a model, not widgets, so a GUI renders it, a CLI prints it, and a
' test asserts it. Expansion is caller-controlled: only directories whose path is
' in the `expanded` set are scanned, so cost is bounded by what is visible.

program main(args)
  load persist from "../stdlib/persist.bas"
  load filetree from "../stdlib/filetree.bas"

  root = "/tmp/gbasic_filetree_example"
  persist.ensure_dir(root + "/src/inner")
  persist.write_text_atomic(root + "/a.txt", "a")
  persist.write_text_atomic(root + "/src/b.txt", "b")
  persist.write_text_atomic(root + "/src/inner/c.txt", "c")

  print "-- collapsed: directories present, contents not scanned"
  for each r in filetree.flatten(filetree.scan(root, []))
    print "  " + repeat("  ", r.depth) + r.kind + " " + r.name
  end for

  print "-- src expanded"
  for each r in filetree.flatten(filetree.scan(root, [root + "/src"]))
    print "  " + repeat("  ", r.depth) + r.kind + " " + r.name
  end for

  print "visible rows collapsed=" + filetree.visible_count(filetree.scan(root, []))
end program
