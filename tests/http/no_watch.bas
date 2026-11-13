' THE CONTROL for watched.bas: the same shape with NO watcher. The loop must
' not be entered, so the program exits at once and delivers nothing -- which is
' what says the WATCH is what admits a handle to the loop, rather than the loop
' running for anything that happens to be in flight.
load http

port = env("HTTP_FIXTURE_PORT")
wait_ms = env("HTTP_NOWATCH_MS")   ' the runner's; 3000 when run by hand
if wait_ms = "" then
    wait_ms = "3000"
end if
h = http.start({ url: "http://127.0.0.1:" + port + "/wait?ms=" + wait_ms })
print "started, no watcher, returning"
