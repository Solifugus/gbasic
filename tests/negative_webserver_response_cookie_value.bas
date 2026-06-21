load webserver
server = webserver.listen(0)
append(server.responses, {id:1, cookies:[12]})
