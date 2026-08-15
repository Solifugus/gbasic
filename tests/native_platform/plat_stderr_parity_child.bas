' PLAT-STDERR: the parity fixture.
'
' Every value shape the runtime can render without an optional native dependency,
' printed TWICE -- once to stdout, once to stderr. The driver then asserts the two
' captured streams are byte-identical, which is a complete parity proof over this
' set: any divergence in argument handling, value rendering, separators or newline
' behaviour shows up as a byte difference.
'
' Deliberately excluded: the handle kinds that need an optional library compiled in
' (postgres/sqlite connections, xml readers, gobjects, gboxed). Those render from
' the same switch as everything here, so excluding them costs coverage of the
' dispatch, not of the mechanism.
function both(v)
    print v
    print to error v
    return nothing
end function

' --- nothing / unknown ---------------------------------------------------
both(nothing)
both(unknown)

' --- numbers: integer, negative, fractional, large, tiny -----------------
both(0)
both(-1)
both(3.5)
both(1000000)
both(0.001)
both(-0.25)

' --- strings: empty, plain, unicode, embedded newline, interior NUL ------
both("")
both("plain text")
both("héllo → wörld ✓")
both("first" + "\n" + "second")
both(from_bytes([72, 0, 73]))

' --- booleans ------------------------------------------------------------
both(true)
both(false)

' --- arrays: empty, numeric, non-numeric, nested -------------------------
' Non-numeric and nested arrays rendered as `[?, ?]` until 2026-08-14, when
' `print` stopped carrying its own renderer and started delegating to the same
' one `string()` uses (PLAT-RENDER). Records rendered as the literal
' `{record}`, and durations as `{duration}`.
both([])
both([1, 2, 3])
both(["a", "b"])
both([[1], [2]])

' --- records -------------------------------------------------------------
both({ a: 1, b: "two" })
both({})

' --- datetimes at every precision, and time-only -------------------------
y(date)= "2027"
both(y)
mo(date)= "2026-06"
both(mo)
d(date)= "2026-05-15"
both(d)
dh(datetime)= "2026-05-15 14"
both(dh)
dm(datetime)= "2026-05-15 14:30"
both(dm)
ds(datetime)= "2026-05-15 14:30:20"
both(ds)
th(time)= "14"
both(th)
tm(time)= "14:30"
both(tm)
ts(time)= "14:30:20"
both(ts)

' --- durations -----------------------------------------------------------
both(2 days 3 hours)
both(45 seconds)

' --- money, including negative (the sign is rendered separately) ---------
price(USD)= 19.95
both(price)
owed(USD)= -5.00
both(owed)
zero(USD)= 0.00
both(zero)

' --- file / dir references ----------------------------------------------
f(file)= "/tmp/gbasic-parity-does-not-exist.txt"
both(f)
dir(dir)= "/tmp"
both(dir)

' --- function value ------------------------------------------------------
fn = both
both(fn)
