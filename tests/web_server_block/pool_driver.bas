' Drives the pooled block: start it, wait for the supervisor's PORT line,
' hit the shared socket, then politely stop the supervisor -- which must
' drain its workers and exit 0 by itself.
program main(args)
  w = process.start({ command: "./gbasic",
                      args: ["--line-buffered", "tests/web_server_block/block_pool.bas"] })
  n = 0
  seen = ""
  port = ""
  while n < 600
    c = process.read(w)
    seen = seen + c.stdout
    nl = find(seen, "\n")
    if nl != nothing then
      line = mid(seen, 0, nl)
      port = mid(line, 5, len(line) - 5)
      break
    end if
    sleep(0.05)
    n = n + 1
  end while
  print "got port: " + string(port != "")
  r = process.run({ command: "curl", args: ["-s", "-m", "10", "http://127.0.0.1:" + port + "/"] })
  print "body: <" + r.stdout + ">"
  process.stop(w)
  st = process.wait(w, 40)
  print "supervisor exit: code=" + string(st.exit_code) + " signal=" + string(st.signal)
  process.release(w)
end program
