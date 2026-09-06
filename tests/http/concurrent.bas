' CONCURRENCY -- the reason this module exists, and the tier that fails if
' `start` secretly blocks. Four requests that each take the same time on the
' server are started TOGETHER; the runner times this program against
' sequential.bas, which fetches the same four through webclient, and requires
' a RATIO rather than an absolute time.
'
' Without this tier every other one in the suite passes on an `http.start`
' that performs the whole request before returning.
load http

port = env("HTTP_FIXTURE_PORT")
handles = []
for i = 1 to 4
    append(handles, http.start({ url: "http://127.0.0.1:" + port + "/wait?ms=400" }))
end for
ok = 0
for each h in handles
    s = http.wait(h)
    if s.status = 200 then
        ok = ok + 1
    end if
end for
print "concurrent ok " + string(ok)
