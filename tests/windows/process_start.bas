' process.start and the live-child verbs -- poll, read, wait, stop, write,
' close_stdin, release -- checked from the outside, on Windows AND Linux.
'
' tests/run_process.sh asserts these on POSIX, but through shell-script helpers
' that CreateProcess cannot run, so on Windows it stops at its first case. This
' file asserts the same contract with gBASIC itself as the child
' (process_child.bas), so ONE file holds both platforms to one behaviour:
'
'     gbasic tests/windows/process_start.bas      (from the repository root)
'
' DETERMINISM IS STRUCTURAL, the run_process.sh rule: a child the parent must
' see MID-RUN blocks on a GATE FILE the parent creates, so "it has printed its
' first line and not its second" is a state the parent controls rather than a
' guess about how fast this machine is. Timing appears only as a bound -- a
' wait that must give up, a stop that must not take thirty seconds.
'
' Every check states its expected answer. "mismatches: 0" is a pass.

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
child = replace(me.script, "process_start.bas", "process_child.bas")
here = replace(me.script, "process_start.bas", "")

' --line-buffered so each line leaves the child as it is printed: a piped
' child otherwise holds its output until exit, and every MID-RUN tier below
' would be measuring stdio's buffer rather than process.start.
function start_child(argv)
    full = ["--line-buffered", child]
    for each a in argv
        append(full, a)
    end for
    return process.start({ command: me.interpreter, args: full })
end function

' Read until `needle` has arrived or `seconds` pass; returns everything read.
function read_until(h, needle, seconds)
    got = ""
    started = epoch(now())
    while not contains(got, needle) and epoch(now()) - started < seconds
        c = process.read(h)
        got = got + c.stdout
        sleep(0.02)
    end while
    return got
end function

print("-- mid-run: the child is observed between its two lines --")
gate {file}= here + "start_gate.tmp"
if exists(gate) then
    delete(gate)
end if
h = start_child(["gate", here + "start_gate.tmp"])
first = read_until(h, "first", 10)
ok("the first line arrived while the child ran", contains(first, "first"), true)
ok("and the second had not", contains(first, "second"), false)
s = process.poll(h)
ok("poll says running", s.running, true)
ok("a running child has no exit code yet", s.exit_code, -1)
write(gate, "go")
second = read_until(h, "second", 10)
ok("the second line arrived after the gate opened", contains(second, "second"), true)
s = process.wait(h, 10)
ok("wait returns once it exits", s.running, false)
ok("a clean exit is success", s.success, true)
ok("exit code 0", s.exit_code, 0)
delete(gate)

print("-- read never blocks --")
gate2 {file}= here + "start_gate2.tmp"
if exists(gate2) then
    delete(gate2)
end if
h = start_child(["gate", here + "start_gate2.tmp"])
seen = read_until(h, "first", 10)
ok("the child is parked at its gate", contains(seen, "first"), true)
t0 = epoch(now())
for i = 1 to 20
    c = process.read(h)
next
ok("twenty reads with nothing to read returned at once", epoch(now()) - t0 < 3, true)
ok("and gave back nothing", c.stdout, "")

print("-- wait with a timeout gives up and says so --")
s = process.wait(h, 0.3)
ok("a wait that expired reports running", s.running, true)
write(gate2, "go")
s = process.wait(h, 10)
ok("and the child still finishes normally", s.success, true)
delete(gate2)

print("-- exit codes and stderr --")
h = start_child(["exit", "7"])
s = process.wait(h, 10)
ok("exit code carried", s.exit_code, 7)
ok("nonzero is not success", s.success, false)
h = start_child(["streams"])
s = process.wait(h, 10)
c = process.read(h)
ok("stdout read after exit", trim(c.stdout), "out")
ok("stderr read separately", trim(c.stderr), "oops")
c = process.read(h)
ok("bytes leave the handle exactly once", c.stdout + c.stderr, "")

print("-- big: both streams past the pipe buffer, drained by wait --")
h = start_child(["big", "20000"])
s = process.wait(h, 60)
c = process.read(h)
ok("the child finished", s.running, false)
ok("all of stdout", len(c.stdout), 2000000)
ok("all of stderr", len(c.stderr), 2000000)

print("-- stdin: a conversation --")
h = process.start({ command: me.interpreter, args: ["--line-buffered", child, "echo"],
                    stdin: "pipe" })
ok("write reports the bytes written", process.write(h, "hello" + chr(10)), 6)
reply = read_until(h, "got hello", 10)
ok("the child answered", contains(reply, "got hello"), true)
ok("a NUL is content, not a terminator", process.write(h, "ab" + chr(0) + "cd" + chr(10)), 6)
process.write(h, "END" + chr(10))
s = process.wait(h, 10)
c = process.read(h)
ok("the conversation ended normally", s.success, true)
ok("and said goodbye", contains(c.stdout, "bye"), true)

on error goto next
h = start_child(["exit", "0"])
process.write(h, "x")
if error then
    ok("writing to a child without stdin: pipe is refused", true, true)
else
    ok("writing to a child without stdin: pipe is refused", false, true)
end if
process.wait(h, 10)

print("-- stop: ended, not waited for --")
h = start_child(["sleep", "30"])
t0 = epoch(now())
s = process.stop(h, { force_after: 2 })
ok("stop with force_after ends a sleeping child", s.running, false)
ok("a stopped child is not success", s.success, false)
ok("and it did not take the 30 seconds", epoch(now()) - t0 < 10, true)

h = start_child(["sleep", "30"])
process.stop(h)
s = process.wait(h, 10)
ok("the polite stop alone ends a child that does not refuse it", s.running, false)

' A CHILD THAT REFUSES THE POLITE STOP, which is the only way to tell the two
' stops apart -- every child above dies on the polite one, so a force_after
' that did nothing would pass them all (measured: it did). No gBASIC program
' can refuse it, so this uses Windows' own `ping`, which answers Ctrl+Break by
' printing its statistics and carrying on. On POSIX the same property is held
' by run_process.sh's SIGTERM-ignoring child.
if contains(lower(me.interpreter), ".exe") then
    print("-- stop: polite is polite, force is force (Windows: ping ignores Ctrl+Break) --")
    h = process.start({ command: "ping", args: ["-n", "30", "127.0.0.1"] })
    sleep(0.5)
    process.stop(h)
    s = process.wait(h, 1)
    ok("the polite stop did NOT kill a child that refused it", s.running, true)
    t0 = epoch(now())
    s = process.stop(h, { force_after: 1 })
    ok("force_after ended it", s.running, false)
    ok("ended, not success", s.success, false)
    ok("and promptly", epoch(now()) - t0 < 10, true)
else
    print("SKIP polite-versus-force (run_process.sh holds it on POSIX)")
end if

print("-- release: done with a child that is still running --")
h = start_child(["sleep", "30"])
t0 = epoch(now())
process.release(h)
ok("release returns at once", epoch(now()) - t0 < 3, true)

print("-- launch failure --")
on error goto next
h = process.start({ command: "gbasic-no-such-program-xyz" })
if error then
    ok("a missing program raises", contains(error.message, "could not execute"), true)
    error.clear()
else
    ok("a missing program raises", false, true)
end if

print("")
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
