' A raise inside a watcher must REPORT. Until PLAT-HTTP it did not: a watcher
' body that raised ended the run with exit 1 and NOTHING on stderr, because the
' fatal report runs when `main` returns and a watcher fires after that.
load http

watch(http.events)
    while count(http.events) > 0
        e = take_first(http.events)
        if e.kind = "done" then
            error { message: "the watcher raised", code: 42 }
        end if
    end while
end watch

h = http.start({ url: "http://127.0.0.1:" + env("HTTP_FIXTURE_PORT") + "/" })
