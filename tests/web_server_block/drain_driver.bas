' Polite TERM on a block server: the on drain hook runs, and the process
' exits by itself with code 0 -- the §7 contract, now one `end server` away.
program main(args)
  w = process.start({ command: "./gbasic",
                      args: ["--line-buffered", "tests/web_server_block/block_drain.bas"] })
  n = 0
  seen = ""
  while n < 400
    c = process.read(w)
    seen = seen + c.stdout
    if find(seen, "PORT ") != nothing then
      break
    end if
    sleep(0.05)
    n = n + 1
  end while
  print "server up"
  process.stop(w)
  st = process.wait(w, 20)
  out = process.read(w)
  has_hook = find(out.stderr, "hook: draining now") != nothing
  print "hook ran: " + string(has_hook)
  print "self-exited: code=" + string(st.exit_code) + " signal=" + string(st.signal)
  process.release(w)
end program
