' PLAT-PROC-STDIN: `process.start({ stdin: "pipe" })`, `process.write` and
' `process.close_stdin`.
'
' WHY IT EXISTS. Until now `process.start` handed back a LIVE CHILD YOU COULD
' ONLY LISTEN TO -- stdout and stderr were pipes and stdin was inherited -- so a
' program could start a subprocess and never say anything to it. Half a pipe.
' That is why no MCP stdio client could be written: the transport is a
' conversation.
'
' THE DEFAULT IS UNCHANGED, and the control below is what says so. A child has
' always inherited its parent's stdin, and an interactive tool launched by a
' gBASIC program would stop working if it were piped without anyone asking --
' so the pipe is OPT-IN and asking for it is a word somebody typed.
'
' Self-checking: a write that silently went nowhere leaves a child reading an
' empty stream, which looks exactly like a child with nothing to say.

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


' Read until something arrives, or the bound expires. gBASIC has no closures, so
' this takes the handle rather than capturing it.
function _await(h, seconds)
    waited = 0
    text = ""
    while waited < seconds
        text = text + process.read(h).stdout
        if len(text) > 0 then
            return text
        end if
        sleep(0.05)
        waited = waited + 0.05
    end while
    return text
end function

' ---- a conversation --------------------------------------------------------
c = process.start({ command: "cat", args: [], stdin: "pipe" })
n = process.write(c, "hello\nworld\n")
check("every byte is written", n, 12)
x = process.close_stdin(c)
st = process.wait(c, 10)
check("the child sees EOF and exits", st.exit_code, 0)
check("and read back what we sent", process.read(c).stdout, "hello\nworld\n")
y = process.release(c)

' ---- more than one write, and the child answers between them ---------------
' A conversation, not a single hand-off: this is the shape a line protocol
' needs and the reason the feature exists.
'
' READ IN A BOUNDED LOOP RATHER THAN AFTER A SLEEP. `process.read` never blocks
' and returns whatever has arrived, so a bare sleep asserts a fact about how
' fast this machine happened to be -- and it fails in the direction that looks
' like the feature is broken. The bound is what keeps a child that never answers
' from hanging the suite instead of failing it.
d = process.start({ command: "cat", args: [], stdin: "pipe" })
w1 = process.write(d, "first\n")
got1 = _await(d, 5)
check("the first line comes back before the second is sent", got1, "first\n")
w2 = process.write(d, "second\n")
check("and then the second", _await(d, 5), "second\n")
z = process.close_stdin(d)
s2 = process.wait(d, 10)
check("the child ends when its input does", s2.exit_code, 0)
q = process.release(d)

' ---- THE CONTROL: the default is unchanged ---------------------------------
' Without a pipe the child still inherits, which is what it has always done --
' and writing is refused with a message naming the option that was not asked
' for, rather than a "broken pipe" that is true about the wrong thing.
on error goto next
e = process.start({ command: "cat", args: [] })
bad = process.write(e, "nope")
if error then
    check("writing to a child whose stdin was not piped is refused",
          contains(error.message, "stdin is not a pipe"), true)
    check("and the message names the option to add",
          contains(error.message, "stdin: \"pipe\""), true)
    error.clear()
end if
k = process.stop(e)
m = process.release(e)

' ---- refusals --------------------------------------------------------------
f = process.start({ command: "cat", args: [], stdin: "socket" })
if error then
    check("an unknown stdin mode is refused, naming what is allowed",
          contains(error.message, "must be \"pipe\" or \"inherit\""), true)
    error.clear()
end if

g = process.start({ command: "cat", args: [], stdin: "inherit" })
check("the control: `inherit` is legal and is the default's own name",
      is_nothing(g), false)
gg = process.stop(g)
ggg = process.release(g)

' ---- a NUL is CONTENT, not a terminator ------------------------------------
' PLAT-NUL's standing lesson: a NUL-truncation defect found in one place is
' evidence about every place that reads a string, and this is a brand new place
' that reads one. A `write` that measured its argument with strlen would send
' three bytes here and report three, and the child would echo three -- an
' ordinary-looking short write with nothing raised.
nulchild = process.start({ command: "cat", args: [], stdin: "pipe" })
payload = "ab" + chr(0) + "cd"
nn = process.write(nulchild, payload)
check("a NUL does not end the write", nn, 5)
p2 = process.close_stdin(nulchild)
s3 = process.wait(nulchild, 10)
back = process.read(nulchild).stdout
check("and every byte comes back", len(back), 5)
check("the NUL among them", byte_at(back, 2), 0)
r3 = process.release(nulchild)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
