' The baseline concurrent.bas is measured against: the same four requests, one
' after another, through the blocking client.
load webclient

port = env("HTTP_FIXTURE_PORT")
ok = 0
for i = 1 to 4
    r = webclient.get("http://127.0.0.1:" + port + "/wait?ms=400")
    if r.status = 200 then
        ok = ok + 1
    end if
end for
print "sequential ok " + string(ok)
