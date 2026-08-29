' PLAT-PROC: a child's environment, and refusing options we do not understand.
'
' Reported by the Transward build, and the first of the two DAMAGED A DESIGN:
' handing a password to OpenSSH needs SSH_ASKPASS and SSH_ASKPASS_REQUIRE, so
' with no env option they had to generate a SHELL WRAPPER SCRIPT per run --
' putting a shell back into the exact code path whose stated principle is that
' nothing is ever parsed as shell syntax. The same block applies to
' GIT_SSH_COMMAND, SSL_CERT_FILE, TZ and DOCKER_HOST.
'
' The second is its companion: `env:` was silently DROPPED before it existed,
' so the mistake looked like the feature working until the child reported an
' empty variable. webserver.listen refuses unknown options by name for exactly
' this reason -- an ignored option there leaves a server on loopback, and here
' it leaves a credential unset.

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

' --------------------------------------------------------------- env
r = process.run({ command: "sh", args: ["-c", "printf %s \"$GB_ONE\""],
                  env: { GB_ONE: "hello" } })
check("env reaches the child", r.stdout, "hello")

' MERGED over the inherited environment, not replacing it: a child that loses
' PATH and HOME to gain one variable is almost never what was meant.
r = process.run({ command: "sh", args: ["-c", "printf %s \"${PATH:+set}\""],
                  env: { GB_ONE: "hello" } })
check("the inherited environment survives", r.stdout, "set")

' `nothing` unsets, which is the only way to hide an inherited variable.
r = process.run({ command: "sh", args: ["-c", "printf %s \"${HOME:+set}\""],
                  env: { HOME: nothing } })
check("nothing unsets a variable", r.stdout, "")

' Several at once, and the parent must be untouched by any of it.
r = process.run({ command: "sh", args: ["-c", "printf %s \"$A$B\""],
                  env: { A: "x", B: "y" } })
check("several variables at once", r.stdout, "xy")
check("the PARENT environment is unchanged", is_unknown(env("A")), true)

' process.start takes it too -- the live-child verb is the one a service uses.
h = process.start({ command: "sh", args: ["-c", "printf %s \"$GB_TWO\""],
                    env: { GB_TWO: "started" } })
process.wait(h)
check("process.start takes env", process.read(h).stdout, "started")

' ----------------------------------------------- refusing what we do not know
on error goto next

r = process.run({ command: "true", envv: { A: "b" } })
check("a misspelled option is refused BY NAME", error.message, "process.run: unknown option 'envv'")
error.clear()

h = process.start({ command: "true", nosuch: 1 })
check("process.start refuses it too", error.message, "process.start: unknown option 'nosuch'")
error.clear()

r = process.run({ command: "true", env: "not a record" })
check("env must be a record", error.message, "process.run: options.env must be a record of name to value")
error.clear()

r = process.run({ command: "true", env: { A: 42 } })
check("an env value must be text or nothing", error.message, "process.run: options.env.A must be a string without NUL, or nothing to unset")
error.clear()

r = process.run({ command: "true", env: { "BAD=NAME": "x" } })
check("an env name cannot contain =", contains(error.message, "an env name must be non-empty"), true)
error.clear()

' THE CONTROL: every option that IS understood must still be accepted, or
' "refuse everything" would satisfy the tier above.
r = process.run({ command: "sh", args: ["-c", "exit 3"], cwd: ".", timeout: 5 })
check("the known options are still accepted", r.exit_code, 3)
error.clear()

on error stop

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
