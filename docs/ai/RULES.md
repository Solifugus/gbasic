# The rules card

Fifteen things a model writing gBASIC gets wrong first, one line and one
example each. `gbasic --rules` prints this card from the build you are
using. Every block below is run by `tests/run_rules_card.sh`: a `' wrong ->`
block must fail with that message, and a `' right ->` block must print
exactly that output (`|` separates lines). So the card cannot drift from the
binary. `UNLEARN.md` has the longer story behind each rule.

**1. Not-equal is `!=`.** `<>` is refused.

```basic
' wrong -> '<>' is not an operator; not-equal is !=
if 1 <> 2 then
    print "differ"
end if
```

```basic
' right -> true
print 1 != 2
```

**2. `mod` is a call, and floored.** The result takes the divisor's sign, so it
differs from QBasic's `MOD`.

```basic
' wrong -> 'mod' is not an operator in gBASIC
print 7 mod 2
```

```basic
' right -> 2
print mod(-7, 3)
```

**3. `+` joins text.** `&` is refused, and `+` joins whenever either side is
text.

```basic
' wrong -> '&' does not join text
print "a" & "b"
```

```basic
' right -> a1
print "a" + 1
```

**4. `print` takes one expression.** It has no `,` or `;` separators.

```basic
' wrong -> `print` takes one expression
print "total", 3
```

```basic
' right -> total 3
print "total " + 3
```

**5. A comment is `'`; a variable is made by assigning to it.** There is no
`rem`, `dim` or `let`.

```basic
' wrong -> `dim` is not a gBASIC statement
dim x
```

```basic
' right -> 0
x = 0   ' a comment
print x
```

**6. A chain is `else if`, two words, closed by one `end if`.**

```basic
' wrong -> `elseif` is not gBASIC; write `else if`
x = 2
if x = 1 then
    print "one"
elseif x = 2 then
    print "two"
end if
```

```basic
' right -> two
x = 2
if x = 1 then
    print "one"
else if x = 2 then
    print "two"
end if
```

**7. Builtins take their arguments in parentheses.** A builtin is never a
statement.

```basic
' wrong -> `sleep` is a function, so its arguments go in parentheses
sleep 0
```

```basic
' right -> done
sleep(0)
print "done"
```

**8. Strings are not indexable, and `mid` is 0-based.**

```basic
' wrong -> use mid for part of a string
s = "hello"
print s[0]
```

```basic
' right -> he
print mid("hello", 0, 2)
```

**9. Names are case-sensitive; keywords are not.**

```basic
' wrong -> undefined variable: name
Name = 1
print name
```

```basic
' right -> 1
Name = 1
PRINT Name
```

**10. `unknown` is SQL's NULL.** A comparison with it is `unknown`, and `if`
refuses one, so give a missing value a default first.

```basic
' wrong -> unknown cannot be used as a condition
x = env("GBASIC_RULES_CARD_UNSET")
if x = "1" then
    print "set"
end if
```

```basic
' right -> unset
if default(env("GBASIC_RULES_CARD_UNSET"), "") = "1" then
    print "set"
else
    print "unset"
end if
```

**11. A missing field raises on `r.b`.** `r["b"]` answers `unknown`.

```basic
' wrong -> unknown record field: b
r = { a: 1 }
print r.b
```

```basic
' right -> unknown
r = { a: 1 }
print r["b"]
```

**12. `find` answers `nothing` on a miss, never `-1`.** Position `0` is a hit.

```basic
' right -> nothing|0
print find([5, 6], 9)
print find([5, 6], 5)
```

**13. A library's function is called by its library's name.**

```basic
' wrong -> a function from another library must be qualified
load dates
print days_between({date}"2026-01-01", {date}"2026-01-03")
```

```basic
' right -> 2
load dates
print dates.days_between({date}"2026-01-01", {date}"2026-01-03")
```

**14. A function cannot assign an outer variable.** Assigning one makes a
local; return the value instead (or change a field of a shared record).

```basic
' wrong -> this assignment creates a new function-local
function bump()
    total = total + 1
end function
total = 0
bump()
```

```basic
' right -> 1
function bump(t)
    return t + 1
end function
total = 0
total = bump(total)
print total
```

**15. Types have gBASIC's names, and the conditional is Python's.** Use
`x:number` (never `int`), and `a if c else b` (never `c ? a : b`).

```basic
' wrong -> 'int' is not a gBASIC type; write x:number
x:int = 5
```

```basic
' right -> pos
x:number = 5
print "pos" if x > 0 else "neg"
```
