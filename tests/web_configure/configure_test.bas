' `web.configure(sv, settings)` -- deployment settings for a declared server.
' Self-checking; run by tests/run_web_configure.sh.
'
' WHY IT IS NOT AN ARGUMENT TO `serve`. A `server` block's head options are
' LITERALS, and that is not a style rule: `server_register` is in the
' interpreter's pre-registration set, so a block is materialised BEFORE
' ANYTHING RUNS -- `workers: n` would read `n` before it was assigned, and
' there is nowhere sound to evaluate an expression. So the declaration says
' what the service IS and configuration says where and how big it runs, and
' `configure` returns ANOTHER DECLARATION: `web.routes`, `web.dispatch` and
' the outline all keep working on the configured value, and configuring twice
' composes.
'
' WHICH OPTIONS ARE ADMITTED IS DERIVED, not chosen. An option may be
' overridden exactly when no parse-time check depends on its value. Measured
' over src/frontend.c that is all seven head options and NOT `host`, whose
' literal value is read to refuse two sites claiming one name.
'
' SELF-CHECKING: every defect here is a plausible RECORD -- a setting silently
' dropped, an original mutated, a field lost in the copy -- and a golden would
' record the damaged configuration as expected.

server app( port: 8080, workers: 1, timeout: 30 )

    get "/"( req )
        return { body: "root" }
    end get

    get "/thing/{id}"( req )
        return { body: req.params.id }
    end get

end server

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

cfg = web.configure(app, { workers: 4, port: 9999 })

' --- the merge ----------------------------------------------------------
check("a configured option takes the new value", cfg.options.workers, 4)
check("and so does another", cfg.options.port, 9999)
check("an option left alone keeps the declared value", cfg.options.timeout, 30)
check("the ORIGINAL declaration is untouched", app.options.workers, 1)
check("in every option", app.options.port, 8080)

' --- it is still a declaration ------------------------------------------
' The whole reason this is `configure` and not an argument to `serve`.
check("the result is still a server declaration", cfg.kind, "server")
check("with the same name", cfg.name, "app")
check("and its routes still resolve", count(web.routes(cfg)), count(web.routes(app)))
d = web.dispatch(cfg, { method: "GET", path: "/thing/42", headers: {}, body: "" })
check("dispatch still works on the configured value", d.body, "42")

' EVERY field is carried, not a named few: a field added to the
' declaration later must travel through untouched rather than be dropped.
check("no field is lost in the copy",
      join(sort(keys(cfg)), ","), join(sort(keys(app)), ","))

' --- composition --------------------------------------------------------
twice = web.configure(cfg, { workers: 7 })
check("configuring twice composes", twice.options.workers, 7)
check("and keeps the earlier setting", twice.options.port, 9999)
check("and the declared one", twice.options.timeout, 30)

' --- all seven kinds ----------------------------------------------------
' The control for the refusals below: a refusal suite with nothing accepted
' is satisfied by refusing everything.
all = web.configure(app, { port: 1, address: "127.0.0.1", inherit: true,
                           workers: 2, timeout: 5,
                           cert: "/tmp/c.pem", key: "/tmp/k.pem" })
check("every head option is settable", count(keys(all.options)), 7)
check("booleans too", all.options.inherit, true)
check("and strings", all.options.address, "127.0.0.1")

' --- refusals, each beside its nearest legal neighbour -------------------
on error goto next

x = web.configure(app, { nope: 1 })
if error then
    check("an option nobody declared is refused by NAME",
          contains(error.message, "has no option 'nope'"), true)
    error.clear()
end if

x = web.configure(app, { host: "a.example" })
if error then
    ' Named separately because `host` IS a real option -- of a `web` site
    ' block -- so "no such option" would be false as well as unhelpful.
    check("host is refused for its own reason",
          contains(error.message, "checked at parse time"), true)
    error.clear()
end if

x = web.configure(app, { workers: "four" })
if error then
    check("a wrongly typed setting is refused",
          contains(error.message, "must be a number"), true)
    error.clear()
end if

x = web.configure(app, { address: 5 })
if error then
    check("in either direction",
          contains(error.message, "must be a string"), true)
    error.clear()
end if

x = web.configure(app, "not a record")
if error then
    check("settings must be a record",
          contains(error.message, "expects a record of settings"), true)
    error.clear()
end if

x = web.configure({ a: 1 }, { workers: 2 })
if error then
    check("and the first argument must be a declaration",
          contains(error.message, "expects a server declaration"), true)
    error.clear()
end if

' A worker count is checked where it is USED, so a literal head and a
' configured value meet the same rule. `workers: 0` would otherwise serve
' single-process without a word.
bad = web.configure(app, { workers: 0 })
check("configure accepts 0 -- it is a number", bad.options.workers, 0)
h = web.serve(bad)
if error then
    check("but serve refuses it, naming the count",
          contains(error.message, "asks for 0 workers"), true)
    error.clear()
end if

' --- WHY THIS EXISTS AT ALL, asserted as a difference -------------------
' The reference already documented a workaround: mutate the record directly,
' `app.options.port = number(conf.port)`. It WORKS -- so the gap was never
' "there is no way" -- but it is entirely UNVALIDATED, and that is the point.
' A misspelled option is silently accepted, creates a field nothing reads, and
' the server runs with the declared value while the author believes otherwise.
' Both halves are asserted, because "configure refuses a typo" alone is
' satisfied by a function nobody needs.
probe = web.configure(app, { workers: 2 })
probe.options.workesr = 9
check("direct mutation accepts a misspelled option in silence",
      has(probe.options, "workesr"), true)
check("and the real setting is untouched, so nothing says anything",
      probe.options.workers, 2)

on error goto next
x = web.configure(app, { workesr: 9 })
if error then
    check("configure refuses the same typo, by name",
          contains(error.message, "has no option 'workesr'"), true)
    error.clear()
end if

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
