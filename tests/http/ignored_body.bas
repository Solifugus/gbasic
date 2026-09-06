' A watcher that never reads the body must still TERMINATE.
'
' The first version of the delivery loop reported the undrained bytes on every
' iteration: an event queue that grew without bound and a loop that never went
' idle, for a program that simply did not want the body. It did not fail -- it
' HUNG, and a hang is not a failure, which is why the runner bounds this one.
load http

events = 0
watch(http.events)
    while count(http.events) > 0
        e = take_first(http.events)
        events = events + 1
    end while
end watch

port = env("HTTP_FIXTURE_PORT")
h = http.start({ url: "http://127.0.0.1:" + port + "/slow?n=3&ms=80" })
print "started, and nothing will ever read it"
