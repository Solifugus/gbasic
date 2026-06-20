load pg
load webserver

function html_escape(text)
    escaped = replace(text, "&", "&amp;")
    escaped = replace(escaped, "<", "&lt;")
    escaped = replace(escaped, ">", "&gt;")
    escaped = replace(escaped, "\"", "&quot;")
    return replace(escaped, "'", "&#39;")
end function

function form_decode_text(text)
    decoded = replace(text, "+", " ")
    decoded = replace(decoded, "%0A", "\n")
    decoded = replace(decoded, "%0D", "")
    decoded = replace(decoded, "%21", "!")
    decoded = replace(decoded, "%22", "\"")
    decoded = replace(decoded, "%23", "#")
    decoded = replace(decoded, "%24", "$")
    decoded = replace(decoded, "%25", "%")
    decoded = replace(decoded, "%26", "&")
    decoded = replace(decoded, "%27", "'")
    decoded = replace(decoded, "%28", "(")
    decoded = replace(decoded, "%29", ")")
    decoded = replace(decoded, "%2C", ",")
    decoded = replace(decoded, "%2F", "/")
    decoded = replace(decoded, "%3A", ":")
    decoded = replace(decoded, "%3B", ";")
    decoded = replace(decoded, "%3D", "=")
    decoded = replace(decoded, "%3F", "?")
    decoded = replace(decoded, "%40", "@")
    return decoded
end function

function form_decode(body)
    form = {}
    if body = "" then
        return form
    end if
    pairs = split(body, "&")
    for each pair in pairs
        parts = split(pair, "=")
        key = form_decode_text(parts[0])
        value = ""
        if len(parts) > 1 then
            value = form_decode_text(join_from(parts, 1, "="))
        end if
        form[key] = value
    end for
    return form
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

function shell_page(title, content)
    return html_page(title, "<main class=\"shell\">" + content + "</main>")
end function

function forum_categories_page(db, req)
    rows = pg.query(db, "select slug, title, description from gbasic_site_categories where hidden = false order by title")
    body = "<h1>Forum</h1><p>Read-only discussion areas backed by Postgres.</p><div class=\"stack\">"
    for each category in rows
        body = body + "<article class=\"list-item\"><h2><a href=\"/forum/" + html_escape(category.slug) + "\">" + html_escape(category.title) + "</a></h2><p>" + html_escape(category.description) + "</p></article>"
    end for
    body = body + "</div><p><a href=\"/\">Back home</a></p>"
    return text_response(req, 200, "text/html; charset=utf-8", shell_page("gBASIC Forum", body))
end function

function category_page(db, req, slug)
    categories = pg.query(db, "select id, slug, title, description from gbasic_site_categories where slug = $1 and hidden = false", [slug])
    if len(categories) = 0 then
        return text_response(req, 404, "text/plain; charset=utf-8", "not found")
    end if

    topics = pg.query(db, "select id, title, author_name, body from gbasic_site_topics where category_id = $1 and hidden = false order by updated_at desc, id desc", [categories[0].id])
    body = "<h1>" + html_escape(categories[0].title) + "</h1><p>" + html_escape(categories[0].description) + "</p><p><a href=\"/forum/" + html_escape(categories[0].slug) + "/new\">Create topic</a></p><div class=\"stack\">"
    for each topic in topics
        body = body + "<article class=\"list-item\"><h2><a href=\"/topic/" + string(topic.id) + "\">" + html_escape(topic.title) + "</a></h2><p>Started by " + html_escape(topic.author_name) + "</p><p>" + html_escape(topic.body) + "</p></article>"
    end for
    body = body + "</div><p><a href=\"/forum\">Back to forum</a></p>"
    return text_response(req, 200, "text/html; charset=utf-8", shell_page(categories[0].title, body))
end function

function new_topic_form_page(db, req, slug)
    categories = pg.query(db, "select slug, title from gbasic_site_categories where slug = $1 and hidden = false", [slug])
    if len(categories) = 0 then
        return text_response(req, 404, "text/plain; charset=utf-8", "not found")
    end if
    body = "<h1>Create topic</h1><form method=\"post\" action=\"/forum/" + html_escape(categories[0].slug) + "/new\"><label>Title<input name=\"title\" required></label><label>Name<input name=\"author_name\" required></label><label>Body<textarea name=\"body\" required></textarea></label><button type=\"submit\">Post topic</button></form><p><a href=\"/forum/" + html_escape(categories[0].slug) + "\">Back to " + html_escape(categories[0].title) + "</a></p>"
    return text_response(req, 200, "text/html; charset=utf-8", shell_page("Create topic", body))
end function

function create_topic(db, req, slug)
    categories = pg.query(db, "select id, slug, title from gbasic_site_categories where slug = $1 and hidden = false", [slug])
    if len(categories) = 0 then
        return text_response(req, 404, "text/plain; charset=utf-8", "not found")
    end if
    form = form_decode(req.body)
    title = trim(form.title)
    author = trim(form.author_name)
    body_text = trim(form.body)
    if title = "" or author = "" or body_text = "" then
        body = "<h1>Create topic</h1><p>Title, name, and body are required.</p><p><a href=\"/forum/" + html_escape(slug) + "/new\">Try again</a></p>"
        return text_response(req, 400, "text/html; charset=utf-8", shell_page("Create topic", body))
    end if
    rows = pg.query(db, "insert into gbasic_site_topics (category_id, title, author_name, body) values ($1, $2, $3, $4) returning id", [categories[0].id, title, author, body_text])
    body = "<h1>Topic created</h1><p><a href=\"/topic/" + string(rows[0].id) + "\">View " + html_escape(title) + "</a></p><p><a href=\"/forum/" + html_escape(categories[0].slug) + "\">Back to " + html_escape(categories[0].title) + "</a></p>"
    return text_response(req, 201, "text/html; charset=utf-8", shell_page("Topic created", body))
end function

function topic_page(db, req, topic_id_text)
    topic_id = number(topic_id_text)
    topics = pg.query(db, "select t.id, t.title, t.author_name, t.body, c.slug as category_slug, c.title as category_title from gbasic_site_topics t join gbasic_site_categories c on c.id = t.category_id where t.id = $1 and t.hidden = false and c.hidden = false", [topic_id])
    if len(topics) = 0 then
        return text_response(req, 404, "text/plain; charset=utf-8", "not found")
    end if

    posts = pg.query(db, "select author_name, body from gbasic_site_posts where topic_id = $1 and hidden = false order by id", [topic_id])
    body = "<p><a href=\"/forum/" + html_escape(topics[0].category_slug) + "\">" + html_escape(topics[0].category_title) + "</a></p><h1>" + html_escape(topics[0].title) + "</h1><article class=\"list-item\"><p>Started by " + html_escape(topics[0].author_name) + "</p><p>" + html_escape(topics[0].body) + "</p></article><h2>Replies</h2><div class=\"stack\">"
    for each post in posts
        body = body + "<article class=\"list-item\"><p>Reply by " + html_escape(post.author_name) + "</p><p>" + html_escape(post.body) + "</p></article>"
    end for
    body = body + "</div><p><a href=\"/topic/" + string(topics[0].id) + "/reply\">Reply</a></p>"
    return text_response(req, 200, "text/html; charset=utf-8", shell_page(topics[0].title, body))
end function

function reply_form_page(db, req, topic_id_text)
    topic_id = number(topic_id_text)
    topics = pg.query(db, "select id, title from gbasic_site_topics where id = $1 and hidden = false and locked = false", [topic_id])
    if len(topics) = 0 then
        return text_response(req, 404, "text/plain; charset=utf-8", "not found")
    end if
    body = "<h1>Reply to " + html_escape(topics[0].title) + "</h1><form method=\"post\" action=\"/topic/" + string(topics[0].id) + "/reply\"><label>Name<input name=\"author_name\" required></label><label>Body<textarea name=\"body\" required></textarea></label><button type=\"submit\">Post reply</button></form><p><a href=\"/topic/" + string(topics[0].id) + "\">Back to topic</a></p>"
    return text_response(req, 200, "text/html; charset=utf-8", shell_page("Reply", body))
end function

function create_reply(db, req, topic_id_text)
    topic_id = number(topic_id_text)
    topics = pg.query(db, "select id, title from gbasic_site_topics where id = $1 and hidden = false and locked = false", [topic_id])
    if len(topics) = 0 then
        return text_response(req, 404, "text/plain; charset=utf-8", "not found")
    end if
    form = form_decode(req.body)
    author = trim(form.author_name)
    body_text = trim(form.body)
    if author = "" or body_text = "" then
        body = "<h1>Reply</h1><p>Name and body are required.</p><p><a href=\"/topic/" + string(topic_id) + "/reply\">Try again</a></p>"
        return text_response(req, 400, "text/html; charset=utf-8", shell_page("Reply", body))
    end if
    pg.exec(db, "insert into gbasic_site_posts (topic_id, author_name, body) values ($1, $2, $3)", [topic_id, author, body_text])
    body = "<h1>Reply posted</h1><p><a href=\"/topic/" + string(topic_id) + "\">Back to " + html_escape(topics[0].title) + "</a></p>"
    return text_response(req, 201, "text/html; charset=utf-8", shell_page("Reply posted", body))
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
        return forum_categories_page(db, req)
    end if
    if req.path = "/forum/general/new" then
        if req.method = "POST" then
            return create_topic(db, req, "general")
        end if
        return new_topic_form_page(db, req, "general")
    end if
    if starts_with(req.path, "/forum/") then
        return category_page(db, req, mid(req.path, 7, len(req.path) - 7))
    end if
    if req.path = "/topic/1/reply" then
        if req.method = "POST" then
            return create_reply(db, req, "1")
        end if
        return reply_form_page(db, req, "1")
    end if
    if starts_with(req.path, "/topic/") then
        return topic_page(db, req, mid(req.path, 7, len(req.path) - 7))
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
