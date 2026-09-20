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
        body = body + "<nav><a href=\"/docs\">Docs</a><a href=\"/examples\">Examples</a><a href=\"/download\">Download</a><a href=\"/about\">About</a><a href=\"/forum\">Forum</a></nav>"
    else
        body = body + "<nav><a href=\"/\">Home</a><a href=\"/docs\">Docs</a><a href=\"/examples\">Examples</a><a href=\"/download\">Download</a><a href=\"/about\">About</a><a href=\"/forum\">Forum</a></nav>"
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

' A whole number in a string, or `unknown`. `number()` RAISES on text that is
' not a number rather than answering `unknown` (measured -- the first version of
' this assumed otherwise, and a request for /topic/not-a-number/reply killed the
' worker, because a raise in a handler is let-it-crash). Frame-scoped
' `on error` makes the guard local, which is why this is four lines and not the
' eleven-`replace` digit strip it replaced.
function whole_number(text)
    on error goto next
    n = number(text)
    if error then
        error.clear()
        return unknown
    end if
    on error stop
    if n != floor(n) then
        return unknown
    end if
    return n
end function

function configured_port()
    env_port = env("GBASIC_SITE_PORT")
    if not is_unknown(env_port) then
        return port_from(trim(env_port), "GBASIC_SITE_PORT")
    end if
    config_file{file}= "examples/gbasic_site/server_port.txt"
    if not exists(config_file) then
        return 0
    end if
    return port_from(trim(read(config_file)), "server_port.txt")
end function

function port_from(text, source)
    if text = "" then
        return 0
    end if
    n = whole_number(text)
    if is_unknown(n) or n < 0 or n > 65535 then
        error source + " must contain a port between 0 and 65535"
    end if
    return n
end function

function topic_id_from_path(text)
    n = whole_number(text)
    if is_unknown(n) then
        return -1
    end if
    return n
end function

function form_id_error(values, key)
    value = form_value(values, key)
    if value = "" then
        return "missing " + key
    end if
    if is_unknown(whole_number(value)) then
        return "invalid " + key
    end if
    return ""
end function

function form_integer_id(values, key)
    return number(form_value(values, key))
end function

' path_after/path_between are gone: the router does this. "/forum/{slug}/new"
' names its own capture, and a pattern beats a capture, so "/forum/general/new"
' cannot be read as a category called "general/new" -- an ordering the chain
' these replaced had to get right by hand and could get wrong in silence.
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

' The shared development CSRF token, from env or a local file. When set, it puts
' anonymous forms in "shared token" mode (used by the integration tests and
' local development). When empty, anonymous forms use the per-visitor
' cookie-bound double-submit token below, which is the production posture.
function shared_csrf_token()
    env_token = env("GBASIC_SITE_CSRF_TOKEN")
    if not is_unknown(env_token) then
        return trim(env_token)
    end if
    token_file{file}= "examples/gbasic_site/csrf_token.txt"
    if not exists(token_file) then
        return ""
    end if
    return trim(read(token_file))
end function

function using_shared_csrf()
    return shared_csrf_token() != ""
end function

function anon_csrf_cookie_name()
    return "gbasic_site_anon_csrf"
end function

' The anonymous CSRF token already presented by this visitor, if any.
function anon_csrf_from_cookie(req)
    raw = req.cookies[anon_csrf_cookie_name()]
    if is_unknown(raw) then
        return ""
    end if
    return trim(raw)
end function

' The token to embed in an anonymous form: reuse the visitor's existing cookie
' token when it looks valid (so multiple tabs/forms agree), otherwise mint a
' fresh one. secure_token(43) is URL-safe ASCII, so len() counts characters.
function anon_csrf_token(req)
    existing = anon_csrf_from_cookie(req)
    if len(existing) >= 20 then
        return existing
    end if
    return secure_token(43)
end function

function anon_csrf_cookie_header(token)
    return anon_csrf_cookie_name() + "=" + token + "; HttpOnly; SameSite=Lax; Path=/; Max-Age=86400"
end function

' The CSRF token to render in an anonymous form for this request.
function effective_csrf_token(req)
    if using_shared_csrf() then
        return shared_csrf_token()
    end if
    return anon_csrf_token(req)
end function

function hidden_input(name, value)
    return "<input type=\"hidden\" name=\"" + html_escape(name) + "\" value=\"" + html_escape(value) + "\">"
end function

function text_input(label, name, maxlength)
    return "<label>" + html_escape(label) + "<input name=\"" + html_escape(name) + "\" maxlength=\"" + string(maxlength) + "\" required></label>"
end function

function password_input(label, name, maxlength)
    return "<label>" + html_escape(label) + "<input type=\"password\" name=\"" + html_escape(name) + "\" maxlength=\"" + string(maxlength) + "\" required></label>"
end function

function text_area(label, name, maxlength)
    return "<label>" + html_escape(label) + "<textarea name=\"" + html_escape(name) + "\" maxlength=\"" + string(maxlength) + "\" required></textarea></label>"
end function

' Render an anonymous form response, attaching the per-visitor CSRF cookie when
' in cookie-bound mode so the browser presents it on the matching POST. `token`
' must be the same value embedded in the form's hidden field.
function form_response(req, status, title, body, token)
    response = shell_response(req, status, title, body)
    if not using_shared_csrf() then
        response.cookies = [anon_csrf_cookie_header(token)]
    end if
    return response
end function

function csrf_valid(req, values)
    submitted = form_value(values, "csrf_token")
    if using_shared_csrf() then
        return submitted = shared_csrf_token()
    end if
    ' Cookie-bound double-submit: the form token must match the token the
    ' visitor was issued in their cookie, and that token must be well-formed.
    cookie_token = anon_csrf_from_cookie(req)
    if len(cookie_token) < 20 then
        return false
    end if
    return submitted = cookie_token
end function

function csrf_forbidden(req)
    return plain_response(req, 403, "invalid csrf token")
end function

' Per-IP anonymous-posting rate limit. Disabled by default (limit 0); set
' GBASIC_SITE_POST_RATE_LIMIT to the max accepted posts per window and
' GBASIC_SITE_POST_RATE_WINDOW to the window length in seconds (default 60).
function post_rate_limit()
    limit_text = env("GBASIC_SITE_POST_RATE_LIMIT")
    if is_unknown(limit_text) then
        return 0
    end if
    limit_text{trimmed}= limit_text
    if limit_text = "" then
        return 0
    end if
    n = whole_number(limit_text)
    if is_unknown(n) or n < 0 then
        error "GBASIC_SITE_POST_RATE_LIMIT must be a non-negative integer"
    end if
    return n
end function

function post_rate_window()
    window_text = env("GBASIC_SITE_POST_RATE_WINDOW")
    if is_unknown(window_text) then
        return 60
    end if
    window_text{trimmed}= window_text
    if window_text = "" then
        return 60
    end if
    n = whole_number(window_text)
    if is_unknown(n) or n < 0 then
        error "GBASIC_SITE_POST_RATE_WINDOW must be a non-negative integer number of seconds"
    end if
    return n
end function

' The client IP for rate limiting. Behind a single trusted reverse proxy the
' socket peer is the proxy, so prefer the last X-Forwarded-For hop (the value
' the proxy itself appended); fall back to the direct socket address.
function client_ip(req)
    forwarded = req.headers["x-forwarded-for"]
    if not is_unknown(forwarded) then
        forwarded{trimmed}= forwarded
        if forwarded != "" then
            hops = split(forwarded, ",")
            last = hops[count(hops) - 1]
            last{trimmed}= last
            if last != "" then
                return last
            end if
        end if
    end if
    return req.remote_ip
end function

' True when this IP has already reached the accepted-post limit for the window.
function post_rate_limited(db, req)
    max_posts = post_rate_limit()
    if max_posts <= 0 then
        return false
    end if
    window = post_rate_window()
    if window <= 0 then
        return false
    end if
    rows = pg.query(db, "select count(*) as n from gbasic_site_post_events where remote_ip = $1 and created_at >= now() - ($2::int * interval '1 second')", [client_ip(req), window])
    return number(rows[0].n) >= max_posts
end function

function record_post_event(db, req, kind)
    ' An effect, not a query: nothing reads the result, so it returns nothing
    ' (the void convention PLAT-WARN exempts by value). It used to return a
    ' boolean, and the two bare calls tripped the unused-result warning --
    ' which failed this suite's clean-stderr check for as long as no Postgres
    ' was reachable to run it.
    if post_rate_limit() <= 0 then
        return nothing
    end if
    pg.exec(db, "insert into gbasic_site_post_events (remote_ip, kind) values ($1, $2)", [client_ip(req), kind])
    return nothing
end function

function rate_limited_response(req)
    body = "<h1>Slow down</h1><p>You are posting too quickly. Please wait a little while and try again.</p><p><a href=\"/forum\">Back to forum</a></p>"
    return shell_response(req, 429, "Slow down", body)
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

function admin_csrf_field(session)
    return hidden_input("csrf_token", session.csrf_token)
end function

function admin_csrf_valid(db, req, values)
    session = current_session(db, req)
    if not (session.authenticated and session.admin) then
        return false
    end if
    return form_value(values, "csrf_token") = session.csrf_token
end function

function admin_request_authorized(db, req, values)
    session = current_session(db, req)
    return session.authenticated and session.admin
end function

function admin_actor(db, req)
    session = current_session(db, req)
    if session.authenticated and session.admin then
        return session.username
    end if
    return ""
end function

function login_page(req)
    body = "<h1>Login</h1><p>Sign in with an admin username and password.</p><form method=\"post\" action=\"/login\">" + text_input("Username", "username", 80) + password_input("Password", "password", 200) + "<button type=\"submit\">Login</button></form><p><a href=\"/forum\">Back to forum</a></p>"
    return shell_response(req, 200, "Login", body)
end function

function login_failed(req)
    body = "<h1>Login</h1><p>Invalid username or password.</p><p><a href=\"/login\">Try again</a></p>"
    return shell_response(req, 403, "Login", body)
end function

function login_submit(db, req)
    form = req.form
    username = trim(form_value(form, "username"))
    password = form_value(form, "password")
    if username = "" or password = "" then
        return login_failed(req)
    end if

    users = pg.query(db, "select id, password_hash, admin from gbasic_site_users where username = $1 and disabled = false", [username])
    if len(users) = 0 then
        return login_failed(req)
    end if
    if not users[0].admin then
        return login_failed(req)
    end if
    if not password_verify(password, users[0].password_hash) then
        return login_failed(req)
    end if

    ' Rotate: invalidate any session referenced by the incoming cookie so a
    ' fixed or stale session id cannot survive a fresh login (fixation defense).
    if not is_unknown(req.cookies["gbasic_site_session"]) then
        prior_id = trim(req.cookies["gbasic_site_session"])
        if prior_id != "" then
            pg.exec(db, "update gbasic_site_sessions set revoked_at = now() where id = $1 and revoked_at is null", [prior_id])
        end if
    end if

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
    token = effective_csrf_token(req)
    body = "<h1>Create topic</h1><form method=\"post\" action=\"/forum/" + html_escape(categories[0].slug) + "/new\">" + hidden_input("csrf_token", token) + text_input("Title", "title", 120) + text_input("Name", "author_name", 80) + text_area("Body", "body", 4000) + "<button type=\"submit\">Post topic</button></form><p><a href=\"/forum/" + html_escape(categories[0].slug) + "\">Back to " + html_escape(categories[0].title) + "</a></p>"
    return form_response(req, 200, "Create topic", body, token)
end function

function create_topic(db, req, slug)
    categories = visible_category(db, slug)
    if len(categories) = 0 then
        return not_found(req)
    end if
    form = req.form
    if not csrf_valid(req, form) then
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
    if post_rate_limited(db, req) then
        return rate_limited_response(req)
    end if
    rows = pg.query(db, "insert into gbasic_site_topics (category_id, title, author_name, body) values ($1, $2, $3, $4) returning id", [categories[0].id, title, author, body_text])
    record_post_event(db, req, "topic")
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
    if not (session.authenticated and session.admin) then
        body = "<h1>Admin</h1><p>Admin sign-in required.</p><p><a href=\"/login\">Login</a></p><p><a href=\"/forum\">Back to forum</a></p>"
        return shell_response(req, 403, "Admin", body)
    end if

    admin_limit = 50
    topics = pg.query(db, "select t.id, t.title, t.author_name, t.hidden, t.moderated_by, c.title as category_title from gbasic_site_topics t join gbasic_site_categories c on c.id = t.category_id order by t.id desc limit $1", [admin_limit])
    posts = pg.query(db, "select p.id, p.author_name, p.body, p.hidden, p.moderated_by, t.title as topic_title from gbasic_site_posts p join gbasic_site_topics t on t.id = p.topic_id order by p.id desc limit $1", [admin_limit])

    body = "<h1>Admin</h1><p>Signed in as " + html_escape(session.username) + ".</p><form class=\"inline-form\" method=\"post\" action=\"/logout\"><button type=\"submit\">Logout</button></form><p>Local moderation tools for hiding topics and replies. Showing the latest " + string(admin_limit) + " topics and replies.</p><h2>Topics</h2><div class=\"stack\">"
    for each topic in topics
        body = body + "<article class=\"list-item\"><h2>" + html_escape(topic.title) + "</h2><p>" + html_escape(moderation_summary(topic)) + " in " + html_escape(topic.category_title) + ", started by " + html_escape(topic.author_name) + "</p>"
        if not topic.hidden then
            body = body + "<form class=\"inline-form\" method=\"post\" action=\"/admin/hide-topic\">" + admin_csrf_field(session) + topic_id_field(topic.id) + "<button type=\"submit\">Hide topic</button></form>"
        else
            body = body + "<form class=\"inline-form\" method=\"post\" action=\"/admin/unhide-topic\">" + admin_csrf_field(session) + topic_id_field(topic.id) + "<button type=\"submit\">Unhide topic</button></form>"
        end if
        body = body + "</article>"
    end for
    body = body + "</div><h2>Replies</h2><div class=\"stack\">"
    for each post in posts
        body = body + "<article class=\"list-item\"><h2>Reply #" + string(post.id) + "</h2><p>" + html_escape(moderation_summary(post)) + " on " + html_escape(post.topic_title) + ", by " + html_escape(post.author_name) + "</p><p>" + html_escape(post.body) + "</p>"
        if not post.hidden then
            body = body + "<form class=\"inline-form\" method=\"post\" action=\"/admin/hide-post\">" + admin_csrf_field(session) + post_id_field(post.id) + "<button type=\"submit\">Hide reply</button></form>"
        else
            body = body + "<form class=\"inline-form\" method=\"post\" action=\"/admin/unhide-post\">" + admin_csrf_field(session) + post_id_field(post.id) + "<button type=\"submit\">Unhide reply</button></form>"
        end if
        body = body + "</article>"
    end for
    body = body + "</div><p><a href=\"/forum\">Back to forum</a></p>"
    return shell_response(req, 200, "Admin", body)
end function

function hide_topic(db, req)
    form = req.form
    if not admin_csrf_valid(db, req, form) then
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
    form = req.form
    if not admin_csrf_valid(db, req, form) then
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
    form = req.form
    if not admin_csrf_valid(db, req, form) then
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
    form = req.form
    if not admin_csrf_valid(db, req, form) then
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
    token = effective_csrf_token(req)
    body = "<h1>Reply to " + html_escape(topics[0].title) + "</h1><form method=\"post\" action=\"/topic/" + string(topics[0].id) + "/reply\">" + hidden_input("csrf_token", token) + text_input("Name", "author_name", 80) + text_area("Body", "body", 4000) + "<button type=\"submit\">Post reply</button></form><p><a href=\"/topic/" + string(topics[0].id) + "\">Back to topic</a></p>"
    return form_response(req, 200, "Reply", body, token)
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
    form = req.form
    if not csrf_valid(req, form) then
        return csrf_forbidden(req)
    end if
    author = trim(form.author_name)
    body_text = trim(form.body)
    validation_error = reply_validation_error(author, body_text)
    if validation_error != "" then
        body = "<h1>Reply</h1><p>" + html_escape(validation_error) + "</p><p><a href=\"/topic/" + string(topic_id) + "/reply\">Try again</a></p>"
        return shell_response(req, 400, "Reply", body)
    end if
    if post_rate_limited(db, req) then
        return rate_limited_response(req)
    end if
    pg.exec(db, "insert into gbasic_site_posts (topic_id, author_name, body) values ($1, $2, $3)", [topic_id, author, body_text])
    pg.exec(db, "update gbasic_site_topics set updated_at = now() where id = $1", [topic_id])
    record_post_event(db, req, "reply")
    body = "<h1>Reply posted</h1><p><a href=\"/topic/" + string(topic_id) + "\">Back to " + html_escape(topics[0].title) + "</a></p>"
    return shell_response(req, 201, "Reply posted", body)
end function

' A handler RETURNS its response and the runtime attaches the request id, so
' the `id: req.id` every one of these used to carry is gone -- and with it the
' class of bug where a response was built for one request and queued against
' another. `file_response` went too: static files are `web.static` now, which
' streams the bytes rather than reading the whole asset into a string.
function text_response(req, status, content_type, body)
    return { status: status,
             headers: { "content-type": content_type },
             body: body }
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

' The router answers an unrouted path with its own 404 ("Not Found"), so this
' one -- for a path that ROUTED but named a topic or category that is not there
' -- says the same words. Two spellings of the same status is the sort of
' difference a reader assumes means something.
function not_found(req)
    return plain_response(req, 404, "Not Found")
end function

function page_response(db, req, slug, include_nav)
    rows = pg.query(db, "select title, body from gbasic_site_pages where slug = $1 and published = true", [slug])
    if len(rows) = 0 then
        return not_found(req)
    end if
    return html_response(req, 200, html_page(rows[0].title, page_body(rows[0], include_nav)))
end function

server site( port: 0 )

    get "/"( req )
        return page_response(G.db, req, "home", true)
    end get

    get "/docs"( req )
        return page_response(G.db, req, "docs", false)
    end get

    get "/examples"( req )
        return page_response(G.db, req, "examples", false)
    end get

    get "/about"( req )
        return page_response(G.db, req, "about", false)
    end get

    ' THE DOWNLOAD, and it is NOT a row in the pages table: page_body runs
    ' html_escape over a page body -- correctly, since that text is edited
    ' through the site -- so a link, a <pre> block and a checksum would all
    ' render as visible markup. It is a route with its own HTML, like /forum.
    get "/download"( req )
        v = G.version
        body = ("<h1>Download</h1>" +
                "<p>gBASIC " + v + " for Linux, x86-64. Extract it anywhere " +
                "and run it &mdash; there is no install step and nothing to set.</p>" +
                "<pre><code>tar xzf gbasic-" + v + "-linux-x86_64.tar.gz\n" +
                "gbasic-" + v + "-linux-x86_64/bin/gbasic</code></pre>" +
                "<p><a href=\"/download/gbasic-" + v + "-linux-x86_64.tar.gz\">" +
                "gbasic-" + v + "-linux-x86_64.tar.gz</a> " +
                "(<a href=\"/download/gbasic-" + v + "-linux-x86_64.tar.gz.sha256\">" +
                "sha256</a>)</p>" +
                "<h2>What it runs on</h2>" +
                "<p>Built against glibc 2.34, so it covers RHEL, Rocky and " +
                "Alma 9, Ubuntu 22.04 LTS and later, Debian 12 and later, and " +
                "anything newer. It is built in a container on the oldest " +
                "supported toolchain for that reason, and the build refuses to " +
                "publish an artifact whose floor has risen.</p>" +
                "<h2>What is in it</h2>" +
                "<p>The language, and 42 of the 62 standard libraries &mdash; " +
                "everything that is pure gBASIC: dates, money, finance, " +
                "accounting, lending, statistics, charts, frames and the rest.</p>" +
                "<p>The other 20 talk to something outside the process &mdash; " +
                "databases, HTTP, XML, GTK &mdash; so they need those libraries " +
                "installed and are not in this build. For those, build from " +
                "source.</p>" +
                "<nav><a href=\"/\">Home</a><a href=\"/docs\">Docs</a>" +
                "<a href=\"/examples\">Examples</a><a href=\"/about\">About</a>" +
                "<a href=\"/forum\">Forum</a></nav>")
        return shell_response(req, 200, "Download gBASIC", body)
    end get

    get "/forum"( req )
        return forum_categories_page(G.db, req)
    end get

    ' A pattern is more specific than a capture, so "/forum/{slug}/new" wins
    ' over "/forum/{slug}" without either being declared first -- the ordering
    ' the hand-rolled chain had to get right by hand, and could get wrong
    ' silently, since a category called "new" would have matched the wrong arm.
    get "/forum/{slug}"( req )
        return category_page(G.db, req, req.params.slug)
    end get

    get "/forum/{slug}/new"( req )
        return new_topic_form_page(G.db, req, req.params.slug)
    end get

    post "/forum/{slug}/new"( req )
        return create_topic(G.db, req, req.params.slug)
    end post

    get "/topic/{id}"( req )
        return topic_page(G.db, req, req.params.id)
    end get

    get "/topic/{id}/reply"( req )
        return reply_form_page(G.db, req, req.params.id)
    end get

    post "/topic/{id}/reply"( req )
        return create_reply(G.db, req, req.params.id)
    end post

    get "/login"( req )
        return login_page(req)
    end get

    post "/login"( req )
        return login_submit(G.db, req)
    end post

    ' Declared POST-only, so a GET is answered 405 with an `Allow` header by
    ' the router rather than by a hand-written branch in every handler.
    post "/logout"( req )
        return logout(G.db, req)
    end post

    get "/admin"( req )
        return admin_page(G.db, req)
    end get

    post "/admin/hide-topic"( req )
        return hide_topic(G.db, req)
    end post

    post "/admin/unhide-topic"( req )
        return unhide_topic(G.db, req)
    end post

    post "/admin/hide-post"( req )
        return hide_post(G.db, req)
    end post

    post "/admin/unhide-post"( req )
        return unhide_post(G.db, req)
    end post

    get "/health"( req )
        return plain_response(req, 200, "ok")
    end get

    get "/static/{path...}"( req )
        return web.static(req.params.path, "examples/gbasic_site/static")
    end get

    ' The database handle is the one thing that must be released politely, and
    ' SIGTERM is the only stop this server has -- there is deliberately no
    ' shutdown route, since an unauthenticated remote kill switch is not
    ' something to ship in the file people are most likely to copy.
    on drain
        pg.close(G.db)
        print "gbasic_site_postgres draining"
    end on

end server

program main( args )
    G = { db: nothing, version: "0.2.1" }

    port_file{file}= "examples/gbasic_site/tmp_port.txt"
    if exists(port_file) then delete(port_file)

    G.db = pg.connect({})
    h = web.serve(web.configure(site, { port: configured_port() }))
    write(port_file, string(h.port))
    print "gbasic_site_postgres listening on 127.0.0.1:" + string(h.port)
end program
