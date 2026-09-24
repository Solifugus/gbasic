' DOGFOOD 31. The server block's head is a generic `IDENT IDENT ( ... )` with
' ZERO reserved words -- deliberately, since `server = webserver.listen(...)`
' appears throughout stdlib -- so ANY two identifiers followed by `()` open
' one. `sub greet()` does, and the parser then swallowed following lines
' looking for a body and an `end`, reporting wherever that search failed.
sub greet()
  print "hi"
end sub
