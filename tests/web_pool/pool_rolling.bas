' The rolling rule, exercised: never retire a worker on faith.
'   * a good deploy rolls v1 -> v2 with service up throughout
'   * a deploy whose source cannot parse fails and costs nothing
'   * a deploy that comes up and dies inside probation fails and costs nothing
program main(args)
  load webserver
  load web
  scratch = args[0]
  server = webserver.listen(0, { hold: true })
  port = string(server.port)

  p = web.pool({ listener: server, count: 2, probation: 0.5,
                 command: "./gbasic",
                 args: ["--line-buffered", "tests/web_pool/worker.bas", "v1", scratch + "/gate.flag"] })
  r = web.pool_start(p)
  p = r.pool
  a = process.run({ command: "curl", args: ["-s", "-m", "5", "http://127.0.0.1:" + port + "/"] })
  print "before roll: <" + a.stdout + ">"

  ' -- a good deploy: point the spec at v2 and roll -------------------------
  p.args = ["--line-buffered", "tests/web_pool/worker.bas", "v2", scratch + "/gate.flag"]
  rr = web.pool_reload(p)
  p = rr.pool
  print "roll ok: " + string(rr.ok) + " rolled=" + string(rr.rolled)
  ' service stayed up and the answer changed
  n = 0
  answered = 0
  while n < 10
    c = process.run({ command: "curl", args: ["-s", "-m", "5", "http://127.0.0.1:" + port + "/"] })
    if c.stdout != "" then
      answered = answered + 1
    end if
    n = n + 1
  end while
  print "10 requests after roll, answered: " + string(answered)
  c = process.run({ command: "curl", args: ["-s", "-m", "5", "http://127.0.0.1:" + port + "/"] })
  print "now serving: <" + c.stdout + ">"

  ' -- a deploy that cannot parse: fails, costs nothing ---------------------
  p.args = ["--line-buffered", "tests/web_pool/broken_worker.bas"]
  rb = web.pool_reload(p)
  p = rb.pool
  print "broken deploy ok: " + string(rb.ok) + " rolled=" + string(rb.rolled)
  print "  why mentions exit: " + string(find(rb.why, "exited before ready") != nothing)
  c = process.run({ command: "curl", args: ["-s", "-m", "5", "http://127.0.0.1:" + port + "/"] })
  print "  still serving: <" + c.stdout + ">"

  ' -- a deploy that dies during probation: fails, costs nothing ------------
  p.args = ["--line-buffered", "tests/web_pool/dying_worker.bas"]
  rd = web.pool_reload(p)
  p = rd.pool
  print "dying deploy ok: " + string(rd.ok)
  print "  why names probation: " + string(find(rd.why, "probation") != nothing)
  c = process.run({ command: "curl", args: ["-s", "-m", "5", "http://127.0.0.1:" + port + "/"] })
  print "  still serving: <" + c.stdout + ">"

  p = web.pool_stop(p)
  print "done, workers=" + string(count(p.workers))
end program
