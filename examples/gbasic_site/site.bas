load webserver

function html_page(title, body)
    return "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>" + title + "</title><link rel=\"stylesheet\" href=\"/static/site.css\"></head><body>" + body + "<script src=\"/static/site.js\"></script></body></html>"
end function

function home_page()
    body = "<main class=\"shell\"><section class=\"hero\"><p class=\"eyebrow\">gBASIC sample app</p><h1>Readable programs, practical web experiments.</h1><p>This local site is written in gBASIC. It will grow into a Postgres-backed home for docs, examples, and a small forum.</p><nav><a href=\"/docs\">Docs</a><a href=\"/forum\">Forum</a></nav></section></main>"
    return html_page("gBASIC", body)
end function

function docs_page()
    body = "<main class=\"shell\"><h1>Docs</h1><p>The docs page is a placeholder for checked-in guides and examples rendered by gBASIC.</p><p><a href=\"/\">Back home</a></p></main>"
    return html_page("gBASIC Docs", body)
end function

function forum_page()
    body = "<main class=\"shell\"><h1>Forum Prototype</h1><p>Forum data will come from Postgres in a later phase.</p><p><a href=\"/\">Back home</a></p></main>"
    return html_page("gBASIC Forum", body)
end function

function text_response(req, status, content_type, body)
    headers = {}
    headers["content-type"] = content_type
    return {
        id:req.id,
        status:status,
        headers:headers,
        body:body
    }
end function

function configured_port()
    env_port = env("GBASIC_SITE_PORT")
    if not is_unknown(env_port) then
        port_text = trim(env_port)
        if port_text = "" then
            return 0
        end if
        if not is_integer_text(port_text) then
            error "GBASIC_SITE_PORT must contain an integer port"
        end if
        port = number(port_text)
        if port < 0 or port > 65535 then
            error "GBASIC_SITE_PORT port must be between 0 and 65535"
        end if
        return port
    end if

    config_file(file)= "examples/gbasic_site/server_port.txt"
    if not exists(config_file) then
        return 0
    end if
    port_text = trim(read(config_file))
    if port_text = "" then
        return 0
    end if
    if not is_integer_text(port_text) then
        error "server_port.txt must contain an integer port"
    end if
    port = number(port_text)
    if port < 0 or port > 65535 then
        error "server_port.txt port must be between 0 and 65535"
    end if
    return port
end function

function is_integer_text(text)
    if text = "" then
        return false
    end if
    rest = replace(text, "0", "")
    rest = replace(rest, "1", "")
    rest = replace(rest, "2", "")
    rest = replace(rest, "3", "")
    rest = replace(rest, "4", "")
    rest = replace(rest, "5", "")
    rest = replace(rest, "6", "")
    rest = replace(rest, "7", "")
    rest = replace(rest, "8", "")
    rest = replace(rest, "9", "")
    return rest = ""
end function

function file_response(req, content_type, source)
    return text_response(req, 200, content_type, read(source))
end function

function log_request(req, response)
    print(req.timestamp + " " + req.remote_ip + " " + req.method + " " + req.path + " " + string(response.status))
end function

function route_request(req)
    if req.path = "/" then
        return text_response(req, 200, "text/html; charset=utf-8", home_page())
    end if
    if req.path = "/docs" then
        return text_response(req, 200, "text/html; charset=utf-8", docs_page())
    end if
    if req.path = "/forum" then
        return text_response(req, 200, "text/html; charset=utf-8", forum_page())
    end if
    if req.path = "/static/site.css" then
        css_file(file)= "examples/gbasic_site/static/site.css"
        return file_response(req, "text/css; charset=utf-8", css_file)
    end if
    if req.path = "/static/site.js" then
        js_file(file)= "examples/gbasic_site/static/site.js"
        return file_response(req, "application/javascript; charset=utf-8", js_file)
    end if
    if req.path = "/health" then
        return text_response(req, 200, "text/plain; charset=utf-8", "ok")
    end if
    return text_response(req, 404, "text/plain; charset=utf-8", "not found")
end function

port_file(file)= "examples/gbasic_site/tmp_port.txt"
if exists(port_file) then delete(port_file)

server = webserver.listen(configured_port())
write(port_file, string(server.port))
print("gbasic_site listening on 127.0.0.1:" + string(server.port))

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)
        if req.path = "/shutdown" then
            response = text_response(req, 200, "text/plain; charset=utf-8", "bye")
            append(server.responses, response)
            log_request(req, response)
            print("gbasic_site shutdown")
            webserver.close(server)
        else
            response = route_request(req)
            append(server.responses, response)
            log_request(req, response)
        end if
    end while
end watch
