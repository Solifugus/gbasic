' A parenthesised expression at the start of a statement.
'
' gBASIC has no bare-expression statement form, so this was never legal. What
' PLAT-CLAUSE changed is the DIAGNOSIS: it used to be read as a modifier clause
' and reported as "unexpected MOD_LPAREN", which pointed at a language feature
' the programmer had not used. It is now an ordinary parenthesis in an
' impossible position, and says so.
program main(args)
  a = 5
  b = 2
  (a - b) > 0
end program
