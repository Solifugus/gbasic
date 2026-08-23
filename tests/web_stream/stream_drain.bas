' Draining a parked stream, deterministically: a worker holds an SSE stream
' that will never finish on its own, TERM lands, and the drain must END the
' stream (the client sees EOF, not a hang) and let the worker exit 0 by
' itself. Without the streaming case in progress_drain, this worker would
' wait forever for a request that already finished arriving.
program main(args)
  load webserver
  load web
  server = webserver.listen(0, { hold: true })
  port = string(server.port)
  w = process.start({ command: "./gbasic",
                      args: ["--line-buffered", "tests/web_stream/stream_worker.bas"],
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

  client = process.start({ command: "curl", args: ["-sN", "-m", "30", "http://127.0.0.1:" + port + "/events"] })
  ' the worker emits "data: open" the moment it parks the stream, so seeing it
  ' on the CLIENT's stdout proves the head and the first event both crossed
  n = 0
  got = ""
  while n < 400
    c = process.read(client)
    got = got + c.stdout
    if find(got, "data: open") != nothing then
      break
    end if
    sleep(0.05)
    n = n + 1
  end while
  print "stream established at the client"

  process.stop(w)
  print "TERM sent (polite)"

  cr = process.wait(client, 20)
  out = process.read(client)
  print "client saw EOF: exit=" + string(cr.exit_code)
  print "stream body: <" + got + out.stdout + ">"
  st = process.wait(w, 20)
  print "worker self-exited: code=" + string(st.exit_code) + " signal=" + string(st.signal)
  process.release(client)
  process.release(w)
end program
