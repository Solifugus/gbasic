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
        body = body + "<nav><a href=\"/docs\">Docs</a><a href=\"/examples\">Examples</a><a href=\"/about\">About</a><a href=\"/forum\">Forum</a></nav>"
    else
        body = body + "<nav><a href=\"/\">Home</a><a href=\"/docs\">Docs</a><a href=\"/examples\">Examples</a><a href=\"/about\">About</a><a href=\"/forum\">Forum</a></nav>"
    end if
    return body + "</section></main>"
end function

function shell_page(title, content)
    return html_page(title, "<main class=\"shell\">" + content + "</main>")
end function

function form_value(values, key)
    if is_unknown(values[key]) then
        return ""
    end if
    return trim(values[key])
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

function topic_id_from_path(text)
    if not is_integer_text(text) then
        return -1
    end if
    return number(text)
end function

function form_id_error(values, key)
    value = form_value(values, key)
    if value = "" then
        return "missing " + key
    end if
    if not is_integer_text(value) then
        return "invalid " + key
    end if
    return ""
end function

function form_integer_id(values, key)
    return number(form_value(values, key))
end function

function path_after(path, prefix)
    return mid(path, len(prefix), len(path) - len(prefix))
end function

function path_between(path, prefix, suffix)
    tail = path_after(path, prefix)
    return left(tail, len(tail) - len(suffix))
end function

function too_long(text, max_len)
    return len(text) > max_len
end function

function topic_validation_error(title, author, body_text)
    if title = "" or author = "" or body_text = "" then
        return "Title, name, and body are required."
    end if
    if too_long(title, 120) then
        return "Title must be 120 characters or fewer."
    end if
    if too_long(author, 80) then
        return "Name must be 80 characters or fewer."
    end if
    if too_long(body_text, 4000) then
        return "Body must be 4000 characters or fewer."
    end if
    return ""
end function

function reply_validation_error(author, body_text)
    if author = "" or body_text = "" then
        return "Name and body are required."
    end if
    if too_long(author, 80) then
        return "Name must be 80 characters or fewer."
    end if
    if too_long(body_text, 4000) then
        return "Body must be 4000 characters or fewer."
    end if
    return ""
end function

function admin_token()
    env_token = env("GBASIC_SITE_ADMIN_TOKEN")
    if not is_unknown(env_token) then
        return trim(env_token)
    end if
    token_file(file)= "examples/gbasic_site/admin_token.txt"
    if not exists(token_file) then
        return ""
    end if
    return trim(read(token_file))
end function

function csrf_token()
    env_token = env("GBASIC_SITE_CSRF_TOKEN")
    if not is_unknown(env_token) then
        return trim(env_token)
    end if
    token_file(file)= "examples/gbasic_site/csrf_token.txt"
    if not exists(token_file) then
        return ""
    end if
    return trim(read(token_file))
end function

function hidden_input(name, value)
    return "<input type=\"hidden\" name=\"" + html_escape(name) + "\" value=\"" + html_escape(value) + "\">"
end function

function text_input(label, name, maxlength)
    return "<label>" + html_escape(label) + "<input name=\"" + html_escape(name) + "\" maxlength=\"" + string(maxlength) + "\" required></label>"
end function

function text_area(label, name, maxlength)
    return "<label>" + html_escape(label) + "<textarea name=\"" + html_escape(name) + "\" maxlength=\"" + string(maxlength) + "\" required></textarea></label>"
end function

function csrf_field()
    return hidden_input("csrf_token", csrf_token())
end function

function csrf_valid(values)
    token = csrf_token()
    if token = "" then
        return false
    end if
    return form_value(values, "csrf_token") = token
end function

function csrf_forbidden(req)
    return plain_response(req, 403, "invalid csrf token")
end function

function admin_authorized(values)
    token = admin_token()
    if token = "" then
        return false
    end if
    return form_value(values, "token") = token
end function

function current_session(db, req)
    if is_unknown(req.cookies["gbasic_site_session"]) then
        return {authenticated:false, admin:false, username:""}
    end if
    session_id = trim(req.cookies["gbasic_site_session"])
    if session_id = "" then
        return {authenticated:false, admin:false, username:""}
    end if
    rows = pg.query(db, "select s.id, s.csrf_token, u.username, u.admin from gbasic_site_sessions s join gbasic_site_users u on u.id = s.user_id where s.id = $1 and s.revoked_at is null and s.expires_at > now() and u.disabled = false", [session_id])
    if len(rows) = 0 then
        return {authenticated:false, admin:false, username:""}
    end if
    return {authenticated:true, admin:rows[0].admin, username:rows[0].username, csrf_token:rows[0].csrf_token}
end function

function admin_request_authorized(db, req, values)
    session = current_session(db, req)
    if session.authenticated and session.admin then
        return true
    end if
    return admin_authorized(values)
end function

function admin_actor(db, req)
    session = current_session(db, req)
    if session.authenticated and session.admin then
        return session.username
    end if
    return "local-admin"
end function

function login_page(req)
    body = "<h1>Login</h1><p>Use the temporary local admin token while password authentication is being built.</p><form method=\"post\" action=\"/login\">" + text_input("Token", "token", 200) + "<button type=\"submit\">Login</button></form><p><a href=\"/forum\">Back to forum</a></p>"
    return shell_response(req, 200, "Login", body)
end function

function login_submit(db, req)
    form = form_decode(req.body)
    if not admin_authorized(form) then
        body = "<h1>Login</h1><p>Invalid local admin token.</p><p><a href=\"/login\">Try again</a></p>"
        return shell_response(req, 403, "Login", body)
    end if

    users = pg.query(db, "insert into gbasic_site_users (username, password_hash, password_algorithm, admin) values ($1, $2, $3, true) on conflict (username) do update set admin = true, disabled = false, updated_at = now() returning id", ["local-admin", "temporary-local-token", "local-token"])
    session_id = secure_token(43)
    csrf = secure_token(43)
    pg.exec(db, "insert into gbasic_site_sessions (id, user_id, csrf_token, expires_at) values ($1, $2, $3, now() + interval '8 hours')", [session_id, users[0].id, csrf])

    response = webserver.redirect(req, "/admin")
    response.cookies = ["gbasic_site_session=" + session_id + "; HttpOnly; SameSite=Lax; Path=/; Max-Age=28800"]
    return response
end function

function logout(db, req)
    if not is_unknown(req.cookies["gbasic_site_session"]) then
        session_id = trim(req.cookies["gbasic_site_session"])
        if session_id != "" then
            pg.exec(db, "update gbasic_site_sessions set revoked_at = now() where id = $1 and revoked_at is null", [session_id])
        end if
    end if
    response = webserver.redirect(req, "/login")
    response.cookies = ["gbasic_site_session=; HttpOnly; SameSite=Lax; Path=/; Max-Age=0"]
    return response
end function

function admin_token_field(token)
    return hidden_input("token", token)
end function

function topic_id_field(topic_id)
    return hidden_input("topic_id", string(topic_id))
end function

function post_id_field(post_id)
    return hidden_input("post_id", string(post_id))
end function

function visible_categories(db)
    return pg.query(db, "select c.slug, c.title, c.description, count(distinct t.id)::text as topic_count, count(p.id)::text as reply_count, coalesce(to_char(max(greatest(t.updated_at, coalesce(p.updated_at, t.updated_at))), 'YYYY-MM-DD HH24:MI'), 'No activity') as latest_activity from gbasic_site_categories c left join gbasic_site_topics t on t.category_id = c.id and t.hidden = false left join gbasic_site_posts p on p.topic_id = t.id and p.hidden = false where c.hidden = false group by c.id, c.slug, c.title, c.description order by c.title")
end function

function visible_category(db, slug)
    return pg.query(db, "select id, slug, title, description from gbasic_site_categories where slug = $1 and hidden = false", [slug])
end function

function replyable_topics(db, topic_id)
    return pg.query(db, "select id, title from gbasic_site_topics where id = $1 and hidden = false and locked = false", [topic_id])
end function

function forum_categories_page(db, req)
    rows = visible_categories(db)
    body = "<h1>Forum</h1><p>Read-only discussion areas backed by Postgres.</p><div class=\"stack\">"
    for each category in rows
        body = body + "<article class=\"list-item\"><h2><a href=\"/forum/" + html_escape(category.slug) + "\">" + html_escape(category.title) + "</a></h2><p>" + html_escape(category.description) + "</p><p>" + html_escape(category.topic_count) + " topics, " + html_escape(category.reply_count) + " replies. Latest activity: " + html_escape(category.latest_activity) + ".</p></article>"
    end for
    body = body + "</div><p><a href=\"/\">Back home</a></p>"
    return shell_response(req, 200, "gBASIC Forum", body)
end function

function category_page(db, req, slug)
    categories = visible_category(db, slug)
    if len(categories) = 0 then
        return not_found(req)
    end if

    topic_limit = 20
    topics = pg.query(db, "select t.id, t.title, t.author_name, t.body, count(p.id)::text as reply_count, to_char(greatest(t.updated_at, coalesce(max(p.updated_at), t.updated_at)), 'YYYY-MM-DD HH24:MI') as latest_activity from gbasic_site_topics t left join gbasic_site_posts p on p.topic_id = t.id and p.hidden = false where t.category_id = $1 and t.hidden = false group by t.id, t.title, t.author_name, t.body, t.updated_at order by greatest(t.updated_at, coalesce(max(p.updated_at), t.updated_at)) desc, t.id desc limit $2", [categories[0].id, topic_limit])
    body = "<h1>" + html_escape(categories[0].title) + "</h1><p>" + html_escape(categories[0].description) + "</p><p><a href=\"/forum/" + html_escape(categories[0].slug) + "/new\">Create topic</a></p><p>Showing the latest " + string(topic_limit) + " topics.</p><div class=\"stack\">"
    for each topic in topics
        body = body + "<article class=\"list-item\"><h2><a href=\"/topic/" + string(topic.id) + "\">" + html_escape(topic.title) + "</a></h2><p>Started by " + html_escape(topic.author_name) + ". " + html_escape(topic.reply_count) + " replies. Latest activity: " + html_escape(topic.latest_activity) + ".</p><p>" + html_escape(topic.body) + "</p></article>"
    end for
    body = body + "</div><p><a href=\"/forum\">Back to forum</a></p>"
    return shell_response(req, 200, categories[0].title, body)
end function

function new_topic_form_page(db, req, slug)
    categories = visible_category(db, slug)
    if len(categories) = 0 then
        return not_found(req)
    end if
    body = "<h1>Create topic</h1><form method=\"post\" action=\"/forum/" + html_escape(categories[0].slug) + "/new\">" + csrf_field() + text_input("Title", "title", 120) + text_input("Name", "author_name", 80) + text_area("Body", "body", 4000) + "<button type=\"submit\">Post topic</button></form><p><a href=\"/forum/" + html_escape(categories[0].slug) + "\">Back to " + html_escape(categories[0].title) + "</a></p>"
    return shell_response(req, 200, "Create topic", body)
end function

function create_topic(db, req, slug)
    categories = visible_category(db, slug)
    if len(categories) = 0 then
        return not_found(req)
    end if
    form = form_decode(req.body)
    if not csrf_valid(form) then
        return csrf_forbidden(req)
    end if
    title = trim(form.title)
    author = trim(form.author_name)
    body_text = trim(form.body)
    validation_error = topic_validation_error(title, author, body_text)
    if validation_error != "" then
        body = "<h1>Create topic</h1><p>" + html_escape(validation_error) + "</p><p><a href=\"/forum/" + html_escape(slug) + "/new\">Try again</a></p>"
        return shell_response(req, 400, "Create topic", body)
    end if
    rows = pg.query(db, "insert into gbasic_site_topics (category_id, title, author_name, body) values ($1, $2, $3, $4) returning id", [categories[0].id, title, author, body_text])
    body = "<h1>Topic created</h1><p><a href=\"/topic/" + string(rows[0].id) + "\">View " + html_escape(title) + "</a></p><p><a href=\"/forum/" + html_escape(categories[0].slug) + "\">Back to " + html_escape(categories[0].title) + "</a></p>"
    return shell_response(req, 201, "Topic created", body)
end function

function moderation_summary(row)
    if not row.hidden then
        return "visible"
    end if
    if is_unknown(row["moderated_by"]) then
        return "hidden"
    end if
    if is_nothing(row.moderated_by) then
        return "hidden"
    end if
    if row.moderated_by = "" then
        return "hidden"
    end if
    return "hidden by " + row.moderated_by
end function

function admin_page(db, req)
    session = current_session(db, req)
    token_authorized = admin_authorized(req.query)
    if not (session.authenticated and session.admin) and not token_authorized then
        body = "<h1>Admin</h1><p>Enter the local moderation token.</p><form method=\"get\" action=\"/admin\">" + text_input("Token", "token", 200) + "<button type=\"submit\">Open admin</button></form><p><a href=\"/login\">Login with a session</a></p><p><a href=\"/forum\">Back to forum</a></p>"
        return shell_response(req, 403, "Admin", body)
    end if

    token = ""
    if token_authorized then
        token = form_value(req.query, "token")
    end if
    admin_limit = 50
    topics = pg.query(db, "select t.id, t.title, t.author_name, t.hidden, t.moderated_by, c.title as category_title from gbasic_site_topics t join gbasic_site_categories c on c.id = t.category_id order by t.id desc limit $1", [admin_limit])
    posts = pg.query(db, "select p.id, p.author_name, p.body, p.hidden, p.moderated_by, t.title as topic_title from gbasic_site_posts p join gbasic_site_topics t on t.id = p.topic_id order by p.id desc limit $1", [admin_limit])

    body = "<h1>Admin</h1><p>Local moderation tools for hiding topics and replies. Showing the latest " + string(admin_limit) + " topics and replies.</p><h2>Topics</h2><div class=\"stack\">"
    if session.authenticated and session.admin then
        body = "<h1>Admin</h1><p>Signed in as " + html_escape(session.username) + ".</p><form class=\"inline-form\" method=\"post\" action=\"/logout\"><button type=\"submit\">Logout</button></form><p>Local moderation tools for hiding topics and replies. Showing the latest " + string(admin_limit) + " topics and replies.</p><h2>Topics</h2><div class=\"stack\">"
    end if
    for each topic in topics
        body = body + "<article class=\"list-item\"><h2>" + html_escape(topic.title) + "</h2><p>" + html_escape(moderation_summary(topic)) + " in " + html_escape(topic.category_title) + ", started by " + html_escape(topic.author_name) + "</p>"
        if not topic.hidden then
            body = body + "<form class=\"inline-form\" method=\"post\" action=\"/admin/hide-topic\">" + csrf_field() + admin_token_field(token) + topic_id_field(topic.id) + "<button type=\"submit\">Hide topic</button></form>"
        else
            body = body + "<form class=\"inline-form\" method=\"post\" action=\"/admin/unhide-topic\">" + csrf_field() + admin_token_field(token) + topic_id_field(topic.id) + "<button type=\"submit\">Unhide topic</button></form>"
        end if
        body = body + "</article>"
    end for
    body = body + "</div><h2>Replies</h2><div class=\"stack\">"
    for each post in posts
        body = body + "<article class=\"list-item\"><h2>Reply #" + string(post.id) + "</h2><p>" + html_escape(moderation_summary(post)) + " on " + html_escape(post.topic_title) + ", by " + html_escape(post.author_name) + "</p><p>" + html_escape(post.body) + "</p>"
        if not post.hidden then
            body = body + "<form class=\"inline-form\" method=\"post\" action=\"/admin/hide-post\">" + csrf_field() + admin_token_field(token) + post_id_field(post.id) + "<button type=\"submit\">Hide reply</button></form>"
        else
            body = body + "<form class=\"inline-form\" method=\"post\" action=\"/admin/unhide-post\">" + csrf_field() + admin_token_field(token) + post_id_field(post.id) + "<button type=\"submit\">Unhide reply</button></form>"
        end if
        body = body + "</article>"
    end for
    body = body + "</div><p><a href=\"/forum\">Back to forum</a></p>"
    return shell_response(req, 200, "Admin", body)
end function

function hide_topic(db, req)
    form = form_decode(req.body)
    if not csrf_valid(form) then
        return csrf_forbidden(req)
    end if
    if not admin_request_authorized(db, req, form) then
        return forbidden(req)
    end if
    id_error = form_id_error(form, "topic_id")
    if id_error != "" then
        return bad_request(req, id_error)
    end if
    topic_id = form_integer_id(form, "topic_id")
    pg.exec(db, "update gbasic_site_topics set hidden = true, moderated_at = now(), moderated_by = $2, updated_at = now() where id = $1", [topic_id, admin_actor(db, req)])
    body = "<h1>Topic hidden</h1><p><a href=\"/admin\">Back to admin</a></p>"
    return shell_response(req, 200, "Topic hidden", body)
end function

function unhide_topic(db, req)
    form = form_decode(req.body)
    if not csrf_valid(form) then
        return csrf_forbidden(req)
    end if
    if not admin_request_authorized(db, req, form) then
        return forbidden(req)
    end if
    id_error = form_id_error(form, "topic_id")
    if id_error != "" then
        return bad_request(req, id_error)
    end if
    topic_id = form_integer_id(form, "topic_id")
    pg.exec(db, "update gbasic_site_topics set hidden = false, moderated_at = null, moderated_by = null, updated_at = now() where id = $1", [topic_id])
    body = "<h1>Topic restored</h1><p><a href=\"/admin\">Back to admin</a></p>"
    return shell_response(req, 200, "Topic restored", body)
end function

function hide_post(db, req)
    form = form_decode(req.body)
    if not csrf_valid(form) then
        return csrf_forbidden(req)
    end if
    if not admin_request_authorized(db, req, form) then
        return forbidden(req)
    end if
    id_error = form_id_error(form, "post_id")
    if id_error != "" then
        return bad_request(req, id_error)
    end if
    post_id = form_integer_id(form, "post_id")
    pg.exec(db, "update gbasic_site_posts set hidden = true, moderated_at = now(), moderated_by = $2, updated_at = now() where id = $1", [post_id, admin_actor(db, req)])
    body = "<h1>Reply hidden</h1><p><a href=\"/admin\">Back to admin</a></p>"
    return shell_response(req, 200, "Reply hidden", body)
end function

function unhide_post(db, req)
    form = form_decode(req.body)
    if not csrf_valid(form) then
        return csrf_forbidden(req)
    end if
    if not admin_request_authorized(db, req, form) then
        return forbidden(req)
    end if
    id_error = form_id_error(form, "post_id")
    if id_error != "" then
        return bad_request(req, id_error)
    end if
    post_id = form_integer_id(form, "post_id")
    pg.exec(db, "update gbasic_site_posts set hidden = false, moderated_at = null, moderated_by = null, updated_at = now() where id = $1", [post_id])
    body = "<h1>Reply restored</h1><p><a href=\"/admin\">Back to admin</a></p>"
    return shell_response(req, 200, "Reply restored", body)
end function

function topic_page(db, req, topic_id_text)
    topic_id = topic_id_from_path(topic_id_text)
    if topic_id < 0 then
        return bad_request(req, "invalid topic id")
    end if
    topics = pg.query(db, "select t.id, t.title, t.author_name, t.body, c.slug as category_slug, c.title as category_title from gbasic_site_topics t join gbasic_site_categories c on c.id = t.category_id where t.id = $1 and t.hidden = false and c.hidden = false", [topic_id])
    if len(topics) = 0 then
        return not_found(req)
    end if

    reply_limit = 50
    posts = pg.query(db, "select author_name, body from gbasic_site_posts where topic_id = $1 and hidden = false order by id limit $2", [topic_id, reply_limit])
    body = "<p><a href=\"/forum/" + html_escape(topics[0].category_slug) + "\">" + html_escape(topics[0].category_title) + "</a></p><h1>" + html_escape(topics[0].title) + "</h1><article class=\"list-item\"><p>Started by " + html_escape(topics[0].author_name) + "</p><p>" + html_escape(topics[0].body) + "</p></article><h2>Replies</h2><p>Showing the first " + string(reply_limit) + " replies.</p><div class=\"stack\">"
    for each post in posts
        body = body + "<article class=\"list-item\"><p>Reply by " + html_escape(post.author_name) + "</p><p>" + html_escape(post.body) + "</p></article>"
    end for
    body = body + "</div><p><a href=\"/topic/" + string(topics[0].id) + "/reply\">Reply</a></p>"
    return shell_response(req, 200, topics[0].title, body)
end function

function reply_form_page(db, req, topic_id_text)
    topic_id = topic_id_from_path(topic_id_text)
    if topic_id < 0 then
        return bad_request(req, "invalid topic id")
    end if
    topics = replyable_topics(db, topic_id)
    if len(topics) = 0 then
        return not_found(req)
    end if
    body = "<h1>Reply to " + html_escape(topics[0].title) + "</h1><form method=\"post\" action=\"/topic/" + string(topics[0].id) + "/reply\">" + csrf_field() + text_input("Name", "author_name", 80) + text_area("Body", "body", 4000) + "<button type=\"submit\">Post reply</button></form><p><a href=\"/topic/" + string(topics[0].id) + "\">Back to topic</a></p>"
    return shell_response(req, 200, "Reply", body)
end function

function create_reply(db, req, topic_id_text)
    topic_id = topic_id_from_path(topic_id_text)
    if topic_id < 0 then
        return bad_request(req, "invalid topic id")
    end if
    topics = replyable_topics(db, topic_id)
    if len(topics) = 0 then
        return not_found(req)
    end if
    form = form_decode(req.body)
    if not csrf_valid(form) then
        return csrf_forbidden(req)
    end if
    author = trim(form.author_name)
    body_text = trim(form.body)
    validation_error = reply_validation_error(author, body_text)
    if validation_error != "" then
        body = "<h1>Reply</h1><p>" + html_escape(validation_error) + "</p><p><a href=\"/topic/" + string(topic_id) + "/reply\">Try again</a></p>"
        return shell_response(req, 400, "Reply", body)
    end if
    pg.exec(db, "insert into gbasic_site_posts (topic_id, author_name, body) values ($1, $2, $3)", [topic_id, author, body_text])
    pg.exec(db, "update gbasic_site_topics set updated_at = now() where id = $1", [topic_id])
    body = "<h1>Reply posted</h1><p><a href=\"/topic/" + string(topic_id) + "\">Back to " + html_escape(topics[0].title) + "</a></p>"
    return shell_response(req, 201, "Reply posted", body)
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

function file_response(req, content_type, source)
    return text_response(req, 200, content_type, read(source))
end function

function html_response(req, status, body)
    return text_response(req, status, "text/html; charset=utf-8", body)
end function

function shell_response(req, status, title, body)
    return html_response(req, status, shell_page(title, body))
end function

function plain_response(req, status, body)
    return text_response(req, status, "text/plain; charset=utf-8", body)
end function

function bad_request(req, message)
    return plain_response(req, 400, message)
end function

function forbidden(req)
    return plain_response(req, 403, "forbidden")
end function

function not_found(req)
    return plain_response(req, 404, "not found")
end function

function method_not_allowed(req)
    return plain_response(req, 405, "method not allowed")
end function

function posting(req)
    return req.method = "POST"
end function

function log_request(req, response)
    print(req.timestamp + " " + req.remote_ip + " " + req.method + " " + req.path + " " + string(response.status))
end function

function page_response(db, req, slug, include_nav)
    rows = pg.query(db, "select title, body from gbasic_site_pages where slug = $1 and published = true", [slug])
    if len(rows) = 0 then
        return not_found(req)
    end if
    return html_response(req, 200, html_page(rows[0].title, page_body(rows[0], include_nav)))
end function

function route_request(db, req)
    if req.path = "/" then
        return page_response(db, req, "home", true)
    end if
    if req.path = "/docs" then
        return page_response(db, req, "docs", false)
    end if
    if req.path = "/examples" then
        return page_response(db, req, "examples", false)
    end if
    if req.path = "/about" then
        return page_response(db, req, "about", false)
    end if
    if req.path = "/forum" then
        return forum_categories_page(db, req)
    end if
    if req.path = "/login" then
        if posting(req) then
            return login_submit(db, req)
        end if
        return login_page(req)
    end if
    if req.path = "/logout" then
        if posting(req) then
            return logout(db, req)
        end if
        return method_not_allowed(req)
    end if
    if req.path = "/admin" then
        return admin_page(db, req)
    end if
    if req.path = "/admin/hide-topic" then
        if posting(req) then
            return hide_topic(db, req)
        end if
        return method_not_allowed(req)
    end if
    if req.path = "/admin/unhide-topic" then
        if posting(req) then
            return unhide_topic(db, req)
        end if
        return method_not_allowed(req)
    end if
    if req.path = "/admin/hide-post" then
        if posting(req) then
            return hide_post(db, req)
        end if
        return method_not_allowed(req)
    end if
    if req.path = "/admin/unhide-post" then
        if posting(req) then
            return unhide_post(db, req)
        end if
        return method_not_allowed(req)
    end if
    if starts_with(req.path, "/forum/") and ends_with(req.path, "/new") then
        category_slug = path_between(req.path, "/forum/", "/new")
        if posting(req) then
            return create_topic(db, req, category_slug)
        end if
        return new_topic_form_page(db, req, category_slug)
    end if
    if starts_with(req.path, "/forum/") then
        return category_page(db, req, path_after(req.path, "/forum/"))
    end if
    if starts_with(req.path, "/topic/") and ends_with(req.path, "/reply") then
        topic_id_text = path_between(req.path, "/topic/", "/reply")
        if posting(req) then
            return create_reply(db, req, topic_id_text)
        end if
        return reply_form_page(db, req, topic_id_text)
    end if
    if starts_with(req.path, "/topic/") then
        return topic_page(db, req, path_after(req.path, "/topic/"))
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
        return plain_response(req, 200, "ok")
    end if
    return not_found(req)
end function

port_file(file)= "examples/gbasic_site/tmp_port.txt"
if exists(port_file) then delete(port_file)

db = pg.connect({})
server = webserver.listen(configured_port())
write(port_file, string(server.port))
print("gbasic_site_postgres listening on 127.0.0.1:" + string(server.port))

watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)
        if req.path = "/shutdown" then
            response = plain_response(req, 200, "bye")
            append(server.responses, response)
            log_request(req, response)
            print("gbasic_site_postgres shutdown")
            pg.close(db)
            webserver.close(server)
        else
            response = route_request(db, req)
            append(server.responses, response)
            log_request(req, response)
        end if
    end while
end watch
