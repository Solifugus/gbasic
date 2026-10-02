' process.run, checked from the outside -- on Windows AND Linux.
'
' The Windows launcher (gb_run_capture in src/platform_win32.c) shares nothing
' with the POSIX fork/exec path except option parsing, so this file asserts the
' SAME behaviour of both. The child is gBASIC itself (process_child.bas), so
' nothing needs installing and both platforms run the identical file:
'
'     gbasic tests/windows/process_run.bas        (from the repository root)
'
' Every check states its expected answer. "mismatches: 0" is a pass.
'
' WHAT EACH TIER IS FOR, because each guards a way the Windows port can be
' wrong WITHOUT raising anything:
'   ARGV  -- Windows has no argv. One command-line string is re-split by the
'            child, so a space, quote or trailing backslash quoted wrongly
'            arrives as different arguments: an ordinary-looking wrong answer.
'   BIG   -- both streams past any pipe buffer at once. A parent that drains
'            one stream at a time does not fail here, it HANGS.
'   TIMEOUT -- asserted against the wall clock, since "timed_out is true" is
'            equally satisfied by a parent that waited the full 30 seconds.

tally = { checks: 0, bad: 0 }

function ok(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print("ok   " + label)
    else
        print("MISMATCH " + label + ": got [" + string(got) + "] want [" + string(want) + "]")
        tally.bad = tally.bad + 1
    end if
end function

me = process.self()
child = replace(me.script, "process_run.bas", "process_child.bas")

function run_child(argv)
    full = [child]
    for each a in argv
        append(full, a)
    end for
    return process.run({ command: me.interpreter, args: full })
end function

print("-- argv: every argument arrives exactly as sent --")
q = chr(34)
b = chr(92)
' "C:\Program Files\" is the case that matters most and the one a naive quoter
' gets wrong: a space forces quoting, and inside quotes a trailing backslash
' must be DOUBLED or it escapes the closing quote. "trailing\" alone needs no
' quoting at all, so it cannot catch that -- found by perturbing the quoter and
' watching this tier stay green until the space was added.
tricky = ["plain", "two words", "", "say " + q + "hi" + q, "trailing" + b,
          "has space" + b, "C:" + b + "Program Files" + b,
          "a" + b + q + "b", b + b + "server" + b + "share", "tab" + chr(9) + "sep",
          "*.txt", "%PATH%", "$HOME"]
sent = ["args"]
for each t in tricky
    append(sent, t)
end for
r = run_child(sent)
ok("the child ran", r.exit_code, 0)
got = decode(trim(r.stdout))
ok("argument count", count(got), count(tricky))
for each t, i in tricky
    if i < count(got) then
        ok("argument " + string(i) + " " + encode(t), got[i], t)
    end if
end for

print("-- streams and exit codes --")
r = run_child(["streams"])
ok("stdout captured", trim(r.stdout), "out")
ok("stderr captured separately", trim(r.stderr), "oops")
ok("a clean exit is success", r.success, true)
r = run_child(["exit", "3"])
ok("exit code carried", r.exit_code, 3)
ok("a nonzero exit is not success", r.success, false)
ok("no signal", r.signal, 0)

print("-- big: both streams past the pipe buffer at once --")
r = run_child(["big", "20000"])
ok("all of stdout", len(r.stdout), 2000000)
ok("all of stderr", len(r.stderr), 2000000)

print("-- env: merged over the inherited environment --")
r = process.run({ command: me.interpreter, args: [child, "env", "GB_PROC_TEST"],
                  env: { GB_PROC_TEST: "set by the parent" } })
ok("a variable the parent set", trim(r.stdout), "set by the parent")
r = process.run({ command: me.interpreter, args: [child, "env", "PATH"],
                  env: { GB_PROC_TEST: "x" } })
ok("PATH still inherited beside it", trim(r.stdout) != "unknown", true)

print("-- cwd: a relative path resolves in the child's directory --")
here = replace(me.script, "process_run.bas", "")
r = process.run({ command: me.interpreter, args: [child, "cat", "process_child.bas"],
                  cwd: here })
ok("the child read a file by a relative name", contains(r.stdout, "A child process for"), true)

print("-- timeout: the child is ended, not waited for --")
started = epoch(now())
r = process.run({ command: me.interpreter, args: [child, "sleep", "30"], timeout: 1 })
elapsed = epoch(now()) - started
ok("timed_out reported", r.timed_out, true)
ok("a timed-out run is not success", r.success, false)
ok("and it did not wait the 30 seconds", elapsed < 10, true)

print("-- launch failure: raised by default, a record on request --")
on error goto next
r = process.run({ command: "gbasic-no-such-program-xyz" })
if error then
    ok("a missing program raises", true, true)
else
    ok("a missing program raises", false, true)
end if
r = process.run({ command: "gbasic-no-such-program-xyz", launch_failure: "result" })
ok("launch_failure result: launch_failed", r.launch_failed, true)
ok("launch_failure result: exit_code -1", r.exit_code, -1)
ok("launch_failure result: why names the command",
   contains(r.why, "gbasic-no-such-program-xyz"), true)

print("")
' THE SELF-CHECK, as in smoke.bas: a run that counted nothing must not pass.
if tally.checks < 30 then
    print("BROKEN: only " + string(tally.checks) + " checks were counted")
    print("MISMATCHES: 1")
else
    print("checks: " + string(tally.checks))
    if tally.bad = 0 then
        print("mismatches: 0")
    else
        print("MISMATCHES: " + string(tally.bad))
    end if
end if
