' PLAT-PROC inside a spawned ACTOR -- the intended consumer. An actor is a separate
' fork+exec'd interpreter process, so its process handles, pipes and SIGCHLD
' bookkeeping are entirely its own; nothing about the parent's handles may interfere
' and vice versa. The actor starts a child, drains it, releases it, and reports.
' The parent runs its OWN child concurrently, so both sides are live at once.
function worker(parent)
  h = process.start({ command: "tests/native_platform/helpers/streams.sh" })
  s = process.wait(h)
  c = process.read(h)
  process.release(h)
  send(parent, { code: s.exit_code, out: c.stdout, err: c.stderr, running: s.running })
  return 0
end function

program main(args)
  a = spawn worker(self())

  mine = process.start({ command: "tests/native_platform/helpers/streams.sh" })
  ms = process.wait(mine)
  mc = process.read(mine)
  process.release(mine)

  m = receive(30 seconds)
  print "actor-code=" + m.code
  print "actor-running=" + m.running
  print "actor-out=<" + m.out + ">"
  print "actor-err=<" + m.err + ">"
  print "parent-code=" + ms.exit_code
  print "parent-out=<" + mc.stdout + ">"
end program
