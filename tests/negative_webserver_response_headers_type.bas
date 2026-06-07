load webserver
server = webserver.listen(0)
append(server.responses, {id:1, headers:"bad"})
