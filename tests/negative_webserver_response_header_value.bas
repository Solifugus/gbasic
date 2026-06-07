load webserver
server = webserver.listen(0)
headers = {test:12}
append(server.responses, {id:1, headers:headers})
