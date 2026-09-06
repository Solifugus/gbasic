' THE CONTROL for watched.bas: the same shape with NO watcher. The loop must
' not be entered, so the program exits at once and delivers nothing -- which is
' what says the WATCH is what admits a handle to the loop, rather than the loop
' running for anything that happens to be in flight.
load http

port = env("HTTP_FIXTURE_PORT")
h = http.start({ url: "http://127.0.0.1:" + port + "/wait?ms=3000" })
print "started, no watcher, returning"
