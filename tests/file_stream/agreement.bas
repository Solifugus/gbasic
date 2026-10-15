program main(args)
  f {file}= args[0]
  nl = chr(10)
  cr = chr(13)
  z = chr(0)
  cases = [
    [ "trailing newline     ", "a" + nl + "b" + nl ],
    [ "no trailing newline  ", "a" + nl + "b" ],
    [ "CRLF                 ", "a" + cr + nl + "b" + cr + nl ],
    [ "empty file           ", "" ],
    [ "just a newline       ", nl ],
    [ "two newlines         ", nl + nl ],
    [ "one line no newline  ", "x" ],
    [ "interior NUL         ", "a" + z + "b" + nl + "plain" ],
    [ "a NUL-only line      ", z + nl + "after" ],
    [ "blank line in middle ", "a" + nl + nl + "c" + nl ],
    [ "CR alone is not a nl ", "a" + cr + "b" + nl ],
    [ "long line            ", repeat("x", 200000) + nl + "short" ]
  ]
  for each c in cases
    write(f, c[1])
    whole = read_lines(f)
    streamed = []
    for each line in f
      append(streamed, line)
    end for
    same = count(whole) = count(streamed)
    if same then
      i = 0
      while i < count(whole)
        if hex_encode(whole[i]) != hex_encode(streamed[i]) then
          same = false
        end if
        i = i + 1
      end while
    end if
    if same then
      print "ok   " + c[0] + " (" + string(count(whole)) + " lines)"
    else
      print "MISMATCH " + c[0] + ": read_lines " + string(count(whole)) + ", streamed " + string(count(streamed))
    end if
  end for
  delete(f)
end program
