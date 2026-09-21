' The gBASIC site, static prototype -- the whole thing is one `server` block.
'
' This file used to be a hand-rolled `if req.path = "/"` chain over
' `webserver.listen` plus HTML built by string concatenation. It predates the
' declarative server block, and a site whose job is to show the platform off
' had stopped using it. Routing, static files and the drain hook are all
' declarations now; what is left is the copy.
'
' Run it from the repository root:
'
'   GBASIC_PATH=stdlib ./gbasic examples/gbasic_site/site.bas
'
' It prints the port and writes it to examples/gbasic_site/tmp_port.txt.
' Stop it with SIGTERM -- there is deliberately no shutdown route, since an
' unauthenticated remote kill switch is not something to ship as an example.
'
' The Postgres-backed site (site_postgres.bas) carries the forum, sessions and
' moderation; this one is the shape without a database.

function page(title, body)
    return ("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">" +
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">" +
            "<title>" + title + "</title>" +
            "<link rel=\"stylesheet\" href=\"/static/site.css\"></head><body>" +
            body +
            "<script src=\"/static/site.js\"></script></body></html>")
end function

function nav()
    return ("<nav><a href=\"/\">Home</a><a href=\"/docs\">Docs</a>" +
            "<a href=\"/examples\">Examples</a><a href=\"/download\">Download</a>" +
            "<a href=\"/about\">About</a></nav>")
end function

function html(title, body)
    return { headers: { "content-type": "text/html; charset=utf-8" },
             body: page(title, body) }
end function

server site( port: 0 )

    get "/"( req )
        body = ("<main class=\"shell\"><section class=\"hero\">" +
                "<p class=\"eyebrow\">gBASIC " + G.version + "</p>" +
                "<h1>A modern BASIC for business programming.</h1>" +
                "<p>Familiar control flow, plus records, first-class functions, " +
                "watchers, shared-nothing actors, and typed values for dates, " +
                "durations and money. Around it sits a working platform: databases, " +
                "a hardened web server, spreadsheets, statistics, charts, a native " +
                "GUI and an AI stack.</p>" +
                "<p>This page is served by a gBASIC program that is one " +
                "<code>server</code> declaration long.</p>" +
                nav() + "</section></main>")
        return html("gBASIC", body)
    end get

    get "/docs"( req )
        body = ("<main class=\"shell\"><h1>Docs</h1>" +
                "<p>The tutorial teaches the language; the reference is the " +
                "complete surface; fifteen cookbooks work through real tasks. " +
                "Every code block and every output block on a cookbook page is " +
                "owned by a file the test suite runs and compares byte for byte, " +
                "so a page cannot drift from the product without a test going " +
                "red.</p>" +
                "<div class=\"stack\">" +
                "<div class=\"list-item\"><h2>Money and business</h2>" +
                "<p>Exact currency across 178 currencies, time value of money, " +
                "double-entry accounting, loan servicing, deposits, credit " +
                "analytics.</p></div>" +
                "<div class=\"list-item\"><h2>Data</h2>" +
                "<p>Spreadsheets read, edited and recalculated in place; " +
                "databases over ODBC, PostgreSQL and SQLite; charts as " +
                "deterministic SVG; what a database estate says about itself.</p>" +
                "</div>" +
                "<div class=\"list-item\"><h2>Applications</h2>" +
                "<p>A declarative web server, native GTK 4 windows, statistics, " +
                "securities analysis, and an AI stack that keeps a conversation " +
                "in a value you can store.</p></div>" +
                "</div>" + nav() + "</main>")
        return html("gBASIC Docs", body)
    end get

    get "/examples"( req )
        body = ("<main class=\"shell\"><h1>Examples</h1>" +
                "<p>The repository carries working programs for files, arrays, " +
                "SQLite, PostgreSQL, web clients, web servers, spreadsheets, " +
                "charts and native windows. This site is one of them.</p>" +
                "<p>They are not illustrations: the same programs are run by the " +
                "test suite on every change, so an example that stopped working " +
                "fails the build.</p>" +
                nav() + "</main>")
        return html("gBASIC Examples", body)
    end get

    get "/about"( req )
        body = ("<main class=\"shell\"><h1>About gBASIC</h1>" +
                "<p>gBASIC takes what BASIC got right -- a program you can read " +
                "aloud -- and grows modern features where they pay for " +
                "themselves. It is implemented as a tree-walking interpreter in " +
                "C11 and ships as a single binary.</p>" +
                "<p>Version " + G.version + " is an early release and the number " +
                "is honest about that. It is not a sketch: " + G.suites +
                " test suites gate every change and the goldens are byte-exact. " +
                "Platform is Linux; macOS and Windows are not tested and no " +
                "support for them is claimed.</p>" +
                nav() + "</main>")
        return html("About gBASIC", body)
    end get

    ' THE DOWNLOAD. Its own route rather than a page of prose, because what a
    ' reader needs here is a link, a checksum and the honest limits -- and every
    ' figure on it is MEASURED (tools/build-release-tarball.sh asserts the glibc
    ' floor; the library count was counted by loading all 62).
    get "/download"( req )
        body = ("<main class=\"shell\"><section class=\"hero\">" +
                "<p class=\"eyebrow\">gBASIC " + G.version + "</p>" +
                "<h1>Download</h1>" +
                "<p>Linux, x86-64. Extract it anywhere and run it &mdash; " +
                "there is no install step and nothing to set.</p>" +
                "<pre><code>tar xzf gbasic-" + G.version + "-linux-x86_64.tar.gz\n" +
                "gbasic-" + G.version + "-linux-x86_64/bin/gbasic</code></pre>" +
                "<p><a href=\"/download/gbasic-" + G.version + "-linux-x86_64.tar.gz\">" +
                "gbasic-" + G.version + "-linux-x86_64.tar.gz</a> " +
                "(<a href=\"/download/gbasic-" + G.version +
                "-linux-x86_64.tar.gz.sha256\">sha256</a>)</p>" +
                "<h2>What it runs on</h2>" +
                "<p>Built against glibc 2.34, so it covers RHEL, Rocky and Alma 9, " +
                "Ubuntu 22.04 LTS and later, Debian 12 and later, and anything " +
                "newer. It is built in a container on the oldest supported " +
                "toolchain for that reason, and the build refuses to publish an " +
                "artifact whose floor has risen.</p>" +
                "<h2>What is in it</h2>" +
                "<p>The language, and 42 of the 62 standard libraries &mdash; " +
                "everything that is pure gBASIC. Dates, money, finance, " +
                "accounting, lending, statistics, charts, frames and the rest " +
                "need nothing but the interpreter.</p>" +
                "<p>The other 20 talk to something outside the process &mdash; " +
                "databases, HTTP, XML, GTK &mdash; so they need those libraries " +
                "present and are not in this build. For those, build from " +
                "source; it is a <code>make</code> away.</p>" +
                nav() + "</section></main>")
        return html("Download gBASIC", body)
    end get

    get "/health"( req )
        return { headers: { "content-type": "text/plain; charset=utf-8" }, body: "ok" }
    end get

    ' Static assets keep the /static/ prefix the deployment's nginx config
    ' serves directly, so the two agree about the URL shape. `web.static`
    ' canonicalizes and then checks containment, so a path climbing out of the
    ' root is refused rather than served.
    get "/static/{path...}"( req )
        return web.static(req.params.path, "examples/gbasic_site/static")
    end get

    on drain
        print "gbasic_site draining"
    end on

end server

program main( args )
    G = { version: "0.2.2", suites: "147" }

    port_file{file}= "examples/gbasic_site/tmp_port.txt"
    if exists(port_file) then delete(port_file)

    h = web.serve(web.configure(site, { port: configured_port() }))
    write(port_file, string(h.port))
    print "gbasic_site listening on 127.0.0.1:" + string(h.port)
end program

' The port comes from the environment or a file, so it is known only at run
' time -- which is exactly what `web.configure` is for: the head keeps its
' literals and the deployment still chooses the port.
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
    n = number(text)
    if is_unknown(n) or n != floor(n) or n < 0 or n > 65535 then
        error source + " must contain a port between 0 and 65535"
    end if
    return n
end function
