' The pool, end to end: hold the port, start workers that inherit it, serve
' through them, drain them. Every wait is on observed state, never on a timer.
program main(args)
  load webserver
  load web
  scratch = args[0]
  server = webserver.listen(0, { hold: true })
  print "held: " + string(server.held)
  port = string(server.port)

  p = web.pool({ listener: server, count: 2,
                 command: "./gbasic",
                 args: ["--line-buffered", "tests/web_pool/worker.bas", "v1", scratch + "/gate.flag"] })
  r = web.pool_start(p)
  p = r.pool
  print "pool up: " + string(r.ok) + " workers=" + string(count(p.workers))

  a = process.run({ command: "curl", args: ["-s", "-m", "5", "http://127.0.0.1:" + port + "/"] })
  print "request 1: <" + a.stdout + ">"
  b = process.run({ command: "curl", args: ["-s", "-m", "5", "http://127.0.0.1:" + port + "/"] })
  print "request 2: <" + b.stdout + ">"

  p = web.pool_stop(p)
  print "stopped: workers=" + string(count(p.workers))

  ' with every worker drained and the supervisor merely HOLDING, a request
  ' gets no answer: connections land in the backlog and nobody serves them
  probe = process.run({ command: "curl", args: ["-s", "-m", "1", "http://127.0.0.1:" + port + "/"] })
  print "after stop, answered: " + string(probe.stdout != "")
end program
