load pg
load webserver

function html_escape(text)
    escaped = replace(text, "&", "&amp;")
    escaped = replace(escaped, "<", "&lt;")
    escaped = replace(escaped, ">", "&gt;")
    escaped = replace(escaped, "\"", "&quot;")
    return replace(escaped, "'", "&#39;")
end function

function html_page(title, body)
    return "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>" + html_escape(title) + "</title><link rel=\"stylesheet\" href=\"/static/site.css\"></head><body>" + body + "<script src=\"/static/site.js\"></script></body></html>"
end function

function page_body(page, include_nav)
    body = "<main class=\"shell\"><section class=\"hero\"><p class=\"eyebrow\">gBASIC sample app</p><h1>" + html_escape(page.title) + "</h1><p>" + html_escape(page.body) + "</p>"
    if include_nav then
        body = body + "<nav><a href=\"/docs\">Docs</a><a href=\"/forum\">Forum</a></nav>"
    else
        body = body + "<p><a href=\"/\">Back home</a></p>"
    end if
    return body + "</section></main>"
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

function page_response(db, req, slug, include_nav)
    rows = pg.query(db, "select title, body from gbasic_site_pages where slug = $1 and published = true", [slug])
    if len(rows) = 0 then
        return text_response(req, 404, "text/plain; charset=utf-8", "not found")
    end if
    return text_response(req, 200, "text/html; charset=utf-8", html_page(rows[0].title, page_body(rows[0], include_nav)))
end function

function route_request(db, req)
    if req.path = "/" then
        return page_response(db, req, "home", true)
    end if
    if req.path = "/docs" then
        return page_response(db, req, "docs", false)
    end if
    if req.path = "/forum" then
        return page_response(db, req, "forum", false)
    end if
    if req.path = "/static/site.css" then
        css_file(file)= "examples/gbasic_site/static/site.css"
        return text_response(req, 200, "text/css; charset=utf-8", read(css_file))
    end if
    if req.path = "/static/site.js" then
        js_file(file)= "examples/gbasic_site/static/site.js"
        return text_response(req, 200, "application/javascript; charset=utf-8", read(js_file))
    end if
    if req.path = "/health" then
        return text_response(req, 200, "text/plain; charset=utf-8", "ok")
    end if
    return text_response(req, 404, "text/plain; charset=utf-8", "not found")
end function

port_file(file)= "examples/gbasic_site/tmp_port.txt"
if exists(port_file) then delete(port_file)

db = pg.connect({})
server = webserver.listen(0)
write(port_file, string(server.port))

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)
        if req.path = "/shutdown" then
            append(server.responses, text_response(req, 200, "text/plain; charset=utf-8", "bye"))
            pg.close(db)
            webserver.close(server)
        else
            append(server.responses, route_request(db, req))
        end if
    end while
end watch
