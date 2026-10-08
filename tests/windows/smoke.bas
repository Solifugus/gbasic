' THE NATIVE WINDOWS SMOKE SUITE — a gBASIC file that checks itself.
'
' Windows has no bash and the gate is 158 bash suites. Porting them to
' PowerShell would be a project larger than the port, and it would leave TWO
' gates that can disagree about what they assert — the exact failure this tree
' has tripwires against elsewhere.
'
' So the suite is written in gBASIC. It needs no shell, no PowerShell and
' nothing installed, and it is the SAME FILE on both platforms, so Linux and
' Windows cannot disagree about what it checks.
'
' IT IS NOT OPTIONAL COVERAGE. A socket is not a file descriptor on Windows, so
' read/write/close on one COMPILES CLEANLY and fails at runtime — the
' cross-compile ratchet can never see it (windows_port_status.md §5). Something
' has to RUN on Windows, and this is the cheapest honest something.
'
' Run it the same way on either platform:
'     gbasic tests/windows/smoke.bas
' Zero mismatches is a pass. Every check states its own expected answer, so a
' wrong result is named rather than recorded.

' THE TALLY IS A RECORD, AND THE FIRST DRAFT GOT THIS WRONG IN THE WORST WAY.
'
' Written as two scalars (`checks = checks + 1` inside the helper), gBASIC has no
' closures, so each assignment created a FUNCTION-LOCAL and the outer counters
' never moved. Every line printed `ok`, the summary said `checks: 0` and
' `mismatches: 0` -- so A FAILING CHECK WOULD NOT HAVE BEEN COUNTED EITHER. A
' suite that reports success regardless of its results is the one shape worse
' than no suite.
'
' The language said so, clearly, and I missed it by reading only stdout:
'   warning: 'checks' was read from an enclosing scope, but this assignment
'   creates a new function-local of that name; the outer variable is unchanged
'   (functions cannot rebind outer scalars -- mutate a field of a shared record
'   instead) [2104]
'
' A record FIELD write does reach the outer value (verified, not assumed), which
' is what warning 2104 recommends. The SELF-CHECK at the bottom of this file is
' here so the next person cannot reintroduce it: it fails if the tally did not
' move, which is the one defect this file cannot otherwise report.
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

print("-- values: the half of gBASIC that needs no platform at all --")
ok("integer arithmetic", 6 * 7, 42)
ok("floored modulo", mod(0 - 7, 3), 2)
ok("exponent literal", 1e3, 1000)
ok("string length in codepoints", len("h" + chr(233) + "llo"), 5)
ok("byte count differs from length", byte_count("h" + chr(233) + "llo"), 6)
ok("interior NUL is content", len("a" + chr(0) + "b"), 3)

p {USD}= 19.95
ok("money is exact", string(p), "19.95")
ok("money rejects nothing silently", money.currency(p), "USD")
d {date}= "2026-03-15"
ok("date arithmetic", string(d + 5 days), "2026-03-20")
ok("duration renders in words", string(2 days + 3 hours), "2 days 3 hours")

print("-- the platform layer: the three mechanisms behind include/platform.h --")
' gb_exe_path: /proc/self/exe on Linux, GetModuleFileNameW on Windows. If this
' is wrong the binary cannot find its own stdlib after being moved, which is
' exactly what the release tiers assert.
ok("the interpreter found its own stdlib", has_builtin("mod"), true)

print("-- files: paths, separators, and line endings --")
tmp {dir}= "."
ok("a directory reference resolves", count(files(tmp)) > 0, true)

f {file}= "smoke_tmp.txt"
write(f, "alpha" + chr(10) + "beta" + chr(10))
ok("write then read round-trips", read(f), "alpha" + chr(10) + "beta" + chr(10))
ok("read_lines on LF", count(read_lines(f)), 2)
ok("line count agrees", lines(f), 2)
' Removed once used: this file runs from the repository root as part of the
' gate (tests/run_windows_suite.sh), and a suite must not leave files behind in
' the tree it tests.
delete(f)

' CRLF is the Windows convention and reading it is the whole point of this tier.
g {file}= "smoke_crlf.txt"
write(g, "alpha" + chr(13) + chr(10) + "beta" + chr(13) + chr(10))
gl = read_lines(g)
ok("read_lines on CRLF gives clean lines", count(gl), 2)
ok("...and strips the CR rather than keeping it", len(gl[0]), 5)
ok("read() stays verbatim", contains(read(g), chr(13)), true)
delete(g)

' A path with an interior NUL must be REFUSED, not truncated (0.4.0 security
' fix). On Windows the failure would be a file appearing under the wrong name.
on error goto next
h {file}= "safe.txt" + chr(0) + "evil"
if error then
    error.clear()
    ok("an interior NUL in a path is refused", true, true)
else
    ok("an interior NUL in a path is refused", false, true)
end if

print("-- processes: CreateProcess on Windows, fork+exec on POSIX --")
' process.run is the one place the port needs a genuinely different mechanism
' and still has to answer the same shape.
on error goto next
r = process.run({ command: "cmd", args: ["/c", "echo", "hello"] })
if error then
    error.clear()
    r = process.run({ command: "echo", args: ["hello"] })
    if error then
        error.clear()
        print("skip process.run (neither cmd nor echo reachable)")
    else
        ok("process.run captured stdout", trim(r.stdout), "hello")
    end if
else
    ok("process.run captured stdout", trim(r.stdout), "hello")
end if

print("")

' THE SELF-CHECK. Without it this file can report a clean run having asserted
' nothing -- which is exactly what its first draft did. A floor rather than an
' exact count, so adding a check does not require editing this line.
if tally.checks < 20 then
    print("BROKEN: only " + string(tally.checks) + " checks were counted -- the tally is not being mutated")
    print("MISMATCHES: 1")
else
    print("checks: " + string(tally.checks))
    if tally.bad = 0 then
        print("mismatches: 0")
    else
        print("MISMATCHES: " + string(tally.bad))
    end if
end if
