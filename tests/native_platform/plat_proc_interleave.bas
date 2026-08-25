' PLAT-PROC: reads and polls INTERLEAVED across a run. Each read consumes only what
' has arrived since the last one, so concatenating every read in order must rebuild
' the child's output exactly -- nothing lost at a boundary, nothing seen twice.
gate = "/tmp/gbasic_plat_proc_interleave.gate"
g{file} = gate
if exists(g) then
  delete(g)
end if

h = process.start({ command: "tests/native_platform/helpers/proc_gated.sh", args: [gate] })

acc = ""
polls_while_running = 0
while find(acc, "CHUNK-ONE") = nothing
  s = process.poll(h)
  if s.running then
    polls_while_running = polls_while_running + 1
  end if
  c = process.read(h)
  acc = acc + c.stdout
  sleep(0.01)
end while
print "saw-poll-while-running=" + (polls_while_running > 0)

write(g, "")

' Alternate poll/read until the child is gone, then one final read for the tail.
done = false
while not done
  s = process.poll(h)
  c = process.read(h)
  acc = acc + c.stdout
  if not s.running then
    done = true
  end if
  sleep(0.01)
end while
c = process.read(h)
acc = acc + c.stdout

print "assembled=<" + acc + ">"
print "bytes=" + byte_count(acc)
process.release(h)
delete(g)
