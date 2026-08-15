' PLAT-RENDER: `print v` and `print string(v)` must render identically.
'
' Run twice by tests/run_render.sh -- once with no argument, once with "string"
' -- over the same value list, and the two captures are required to be
' byte-identical. That is a structural parity proof rather than a transcript:
' it cannot drift, because a change to either renderer moves one side only and
' fails here.
'
' This exists because the two DID drift, silently and for a long time. `print`
' carried its own switch that understood numbers inside an array and nothing
' else, so print(["a","b"]) was `[?, ?]` and print({a:1}) was the literal word
' `{record}`, while string() rendered both correctly. Nothing failed, because
' the goldens had captured the broken output as expected -- two committed
' goldens contained `[?, ?]` and `{record}` and were defending them.
'
' `print` now delegates to the same renderer string() uses, so the only way to
' keep them honest is to assert it on every value shape.

' Note: the mode is threaded through as a parameter rather than read from a
' top-level variable. gBASIC functions are references, not closures -- they do
' not see the enclosing scope, and reading `mode` inside `show` raises
' "undefined variable: mode".
function show(mode, v)
    if mode = "string" then
        print string(v)
    else
        print v
    end if
    return nothing
end function

program main(args)
    ' Initialised unconditionally first: gBASIC has no declarations, so a
    ' variable assigned only inside an `if` does not exist when the branch does
    ' not run, and reading it raises "undefined variable".
    mode = ""
    if count(args) > 0 then
        mode = args[0]
    end if

    ' --- nothing / unknown ---------------------------------------------------
    show(mode, nothing)
    show(mode, unknown)

    ' --- numbers -------------------------------------------------------------
    show(mode, 0)
    show(mode, -1)
    show(mode, 3.5)
    show(mode, 1000000)
    show(mode, 0.1 + 0.2)
    show(mode, 1 / 3)

    ' --- strings, including the byte-level awkward ones -----------------------
    show(mode, "")
    show(mode, "plain text")
    show(mode, "héllo → wörld ✓")
    show(mode, "first" + "\n" + "second")
    show(mode, from_bytes([72, 0, 73]))
    show(mode, "has \"quotes\" inside")

    ' --- booleans ------------------------------------------------------------
    show(mode, true)
    show(mode, false)

    ' --- arrays: the shapes that used to render as [?, ?] --------------------
    show(mode, [])
    show(mode, [1, 2, 3])
    show(mode, ["a", "b"])
    show(mode, [[1], [2]])
    show(mode, [1, "two", true])
    show(mode, [{ a: 1 }, { a: 2 }])
    show(mode, [nothing, unknown])

    ' --- NESTED NUMBERS: a value must not change shape by being contained ----
    ' The first version of this fixture had nested INTEGERS only, which render
    ' the same under any formatter, so it missed that nested numbers went
    ' through a second formatter still using "%.17g": print(0.1) was `0.1` while
    ' print([0.1]) was `[0.10000000000000001]`. The parity tier could not catch
    ' it either -- `print` and `string()` agreed with each other, both wrong.
    ' Agreement is not correctness, which is why these are here.
    show(mode, [0.1])
    show(mode, [1 / 3])
    show(mode, [0.1 + 0.2])
    show(mode, { x: 0.1 })
    show(mode, [[0.1]])
    show(mode, { deep: { frac: 1 / 3 } })
    show(mode, [265550.75, 23750.25])

    ' --- records: used to render as the literal {record} ---------------------
    show(mode, { })
    show(mode, { a: 1, b: "two" })
    show(mode, { nested: { deep: [1, 2] } })
    show(mode, { list: ["x", "y"] })

    ' --- datetimes at every precision, and time-only -------------------------
    y(date)= "2027"
    show(mode, y)
    mo(date)= "2026-06"
    show(mode, mo)
    d(date)= "2026-05-15"
    show(mode, d)
    dh(datetime)= "2026-05-15 14"
    show(mode, dh)
    dm(datetime)= "2026-05-15 14:30"
    show(mode, dm)
    ds(datetime)= "2026-05-15 14:30:20"
    show(mode, ds)
    th(time)= "14"
    show(mode, th)
    tm(time)= "14:30"
    show(mode, tm)
    ts(time)= "14:30:20"
    show(mode, ts)

    ' --- durations: used to render as the literal {duration} -----------------
    show(mode, 2 days 3 hours)
    show(mode, 45 seconds)
    show(mode, 1 day)
    show(mode, 1 hour 1 minute 1 second)
    show(mode, 2 years 6 months)
    show(mode, 0 seconds)

    ' --- money, including negative and zero ----------------------------------
    price(USD)= 19.95
    show(mode, price)
    owed(USD)= -5.00
    show(mode, owed)
    zed(USD)= 0.00
    show(mode, zed)

    ' --- file / dir ----------------------------------------------------------
    f(file)= "/tmp/gbasic-render-does-not-exist.txt"
    show(mode, f)
    dd(dir)= "/tmp"
    show(mode, dd)

    ' --- function and regex values -------------------------------------------
    show(mode, show)
    show(mode, regex("a+"))

    ' --- TOTALITY: typed values NESTED inside a compound ---------------------
    ' Every one of these raised before the display mode existed, because
    ' string() reached them through the JSON encoder, which legitimately refuses
    ' a date or a function. Displaying a value must never be able to end the
    ' program, so display is a separate, total mode. `encode` still refuses --
    ' asserted as a negative in the runner, since that refusal is correct.
    show(mode, [d])
    show(mode, [price])
    show(mode, [2 days])
    show(mode, [show])
    show(mode, [regex("a+")])
    show(mode, { when: d, cost: price })
    show(mode, { fn: show })
    show(mode, [[d], [{ inner: price }]])
end program
