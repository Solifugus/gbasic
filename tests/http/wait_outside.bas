' THE CONTROL: the same `http.wait`, outside the loop. It must be SILENT.
' Without this the warning is indistinguishable from one that fires on every
' wait, which would make it noise authors learn to ignore.
load http

s = http.wait(http.start({ url: "http://127.0.0.1:" + env("HTTP_FIXTURE_PORT") + "/" }))
print "waited at top level: " + string(s.status)
