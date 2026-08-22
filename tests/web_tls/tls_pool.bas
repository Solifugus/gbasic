program main(args)
  load webserver
  load web
  base = args[0]
  server = webserver.listen(0, { hold: true })
  p = web.pool({ listener: server, count: 2, command: "./gbasic",
                 args: ["--line-buffered", "tests/web_tls/tls_worker.bas", base] })
  r = web.pool_start(p)
  p = r.pool
  print "PORT " + string(server.port)
  print "pool up: " + string(r.ok)
  ' stay alive while the runner probes; drain when it creates the stop flag
  stopflag(file) = base + "/stop.flag"
  while not exists(stopflag)
    t = web.pool_tick(p)
    p = t.pool
    sleep(0.1)
  end while
  p = web.pool_stop(p)
  print "stopped"
end program
