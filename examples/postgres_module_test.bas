load pg

on error goto next
db = pg.connect({
    host:"127.0.0.1",
    port:1,
    connect_timeout:1
})
print(error.source)
error.clear()
