' PLAT-OUTLINE test driver. Dispatches on args[0] to a named inline source
' fixture, runs source_outline(text), and prints a deterministic, path-free
' canonical dump. Every node's slice is extracted by BYTE offset (byte_at +
' from_bytes) because the outline's offsets are absolute byte offsets, so the
' slice proves the range under the platform's byte convention. Newlines/tabs are
' escaped so each record stays on one line for golden comparison.

function byte_slice(s, start, length)
  arr = []
  i = start
  while i < start + length
    arr = append(arr, byte_at(s, i))
    i = i + 1
  end while
  return from_bytes(arr)
end function

function node_by_id(nodes, id)
  for each n in nodes
    if n.id = id then
      return n
    end if
  end for
  return nothing
end function

function esc(s)
  s = replace(s, "\n", "\\n")
  s = replace(s, "\t", "\\t")
  return s
end function

function source_for(mode)
  if mode = "empty" then
    return ""
  end if
  if mode = "one" then
    return "x = 1\n"
  end if
  if mode = "program" then
    return "program main(args)\n  print \"hi\"\nend program\n"
  end if
  if mode = "functions" then
    return "function add(a, b)\n  return a + b\nend function\n\nfunction obj.method(self)\n  return self\nend function\n"
  end if
  if mode = "modifiers" then
    return "modifier rounded(n) for compare\n  return n\nend modifier\n\nexport modifier shout for assign\n  return upper(value)\nend modifier\n"
  end if
  if mode = "nested" then
    return "program main(args)\n  if x > 0 then\n    for each n in items\n      while n > 0\n        print n\n      end while\n    end for\n  else\n    print 0\n  end if\nend program\n"
  end if
  if mode = "consider" then
    return "consider color\nif \"r\" then\n  print 1\nif \"g\" then\n  print 2\nelse\n  print 3\nend consider\n"
  end if
  if mode = "multiline" then
    return "nums = [\n  1,\n  2,\n  3\n]\n"
  end if
  if mode = "comments" then
    return "' leading comment\n\nx = 1  ' trailing\n\n' another\ny = 2\n"
  end if
  if mode = "invalid" then
    return "function add(a, b)\n  return a +\n"
  end if
  if mode = "unmatched" then
    return "program main(args)\n  if x > 0 then\n    print x\nend program\n"
  end if
  if mode = "unicode" then
    return "greeting = \"héllo wörld\"\nprint greeting\n"
  end if
  return ""
end function

program main(args)
  mode = ""
  if count(args) > 0 then
    mode = args[0]
  end if
  src = source_for(mode)
  o = source_outline(src)
  print "schema_version=" + o.schema_version + " ok=" + o.ok + " nodes=" + count(o.nodes) + " diagnostics=" + count(o.diagnostics)
  for each nd in o.nodes
    pid = "-"
    if nd.parent_id != nothing then
      pid = "" + nd.parent_id
    end if
    nm = "-"
    if nd.name != nothing then
      nm = nd.name
    end if
    sl = esc(byte_slice(src, nd.start_offset, nd.end_offset - nd.start_offset))
    print "#" + nd.id + " parent=" + pid + " " + nd.kind + " name=" + nm + " [" + nd.start_offset + "," + nd.end_offset + ") " + nd.start_line + ":" + nd.start_column + ".." + nd.end_line + ":" + nd.end_column + " block=" + nd.flags.block + " exp=" + nd.flags.exported + " att=" + nd.flags.attached + " slice=<" + sl + ">"
  end for
  for each d in o.diagnostics
    print "! " + d.severity + " [" + d.start_offset + "," + d.end_offset + ") " + d.start_line + ":" + d.start_column + ".." + d.end_line + ":" + d.end_column + " msg=" + d.message
  end for
  bad = 0
  for each nd in o.nodes
    if nd.parent_id != nothing then
      p = node_by_id(o.nodes, nd.parent_id)
      if p = nothing then
        bad = bad + 1
      else
        if nd.start_offset < p.start_offset then
          bad = bad + 1
        end if
        if nd.end_offset > p.end_offset then
          bad = bad + 1
        end if
      end if
    end if
  end for
  print "containment_violations=" + bad
end program
