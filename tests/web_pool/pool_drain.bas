' The drain contract, deterministically: a request is held in flight by a gate
' file, TERM lands, and then -- with the signal already delivered -- the gate
' opens. The response must still arrive, the worker must exit 0 by itself, and
' a connection attempted mid-drain must go unanswered.
program main(args)
  load webserver
  load web
  scratch = args[0]
  gatepath = scratch + "/gate.flag"
  gate{file} = gatepath
  if exists(gate) then
    delete(gate)
  end if
  server = webserver.listen(0, { hold: true })
  port = string(server.port)
  w = process.start({ command: "./gbasic",
                      args: ["--line-buffered", "tests/web_pool/worker.bas", "v1", gatepath],
                      listen_fds: [server] })
  n = 0
  seen = ""
  while n < 400
    c = process.read(w)
    seen = seen + c.stdout
    if find(seen, "READY") != nothing then
      break
    end if
    sleep(0.05)
    n = n + 1
  end while
  print "worker ready"

  slow = process.start({ command: "curl", args: ["-s", "-m", "30", "http://127.0.0.1:" + port + "/slow"] })
  ' wait until the worker SAYS the request is parked in its handler. (A single
  ' worker cannot answer anything else while it is parked -- one interpreter
  ' thread per worker is the whole reason pools exist -- so a ping here would
  ' hang, not prove in-flight.)
  n = 0
  story = ""
  while n < 400
    c = process.read(w)
    story = story + c.stderr
    if find(story, "in-handler /slow") != nothing then
      break
    end if
    sleep(0.05)
    n = n + 1
  end while
  print "in flight confirmed by the worker itself"

  process.stop(w)
  print "TERM sent (polite)"

  ' mid-drain: the worker's listener is closed; nobody answers a new request
  probe = process.run({ command: "curl", args: ["-s", "-m", "1", "http://127.0.0.1:" + port + "/"] })
  print "mid-drain request answered: " + string(probe.stdout != "")

  write(gate, "go")
  sr = process.wait(slow, 20)
  out = process.read(slow)
  print "in-flight response: <" + out.stdout + ">"
  st = process.wait(w, 20)
  print "worker self-exited: code=" + string(st.exit_code) + " signal=" + string(st.signal)
  process.release(slow)
  process.release(w)
end program
