' PLAT-PROC byte fidelity across a chunk boundary. The child splits its output
' MID-CODEPOINT (the two-byte e-acute straddles the two halves) and ends with no
' trailing newline. Reassembling the reads must reproduce every byte exactly.
gate = "/tmp/gbasic_plat_proc_bytes.gate"
g{file} = gate
if exists(g) then
  delete(g)
end if

h = process.start({ command: "tests/native_platform/helpers/proc_bytes.sh", args: [gate] })

' First half is 20 bytes and ends on the FIRST byte of the multi-byte character.
acc = ""
while byte_count(acc) < 20
  c = process.read(h)
  acc = acc + c.stdout
  sleep(0.01)
end while
print "half1_bytes=" + byte_count(acc)

write(g, "")
s = process.wait(h)
c = process.read(h)
acc = acc + c.stdout

print "total_bytes=" + byte_count(acc)
print "exit_code=" + s.exit_code
print "assembled=<" + acc + ">"
delete(g)
process.release(h)
