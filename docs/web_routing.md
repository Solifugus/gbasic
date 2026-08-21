# `web` — a route table as data

`stdlib/web.bas`. Pure gBASIC; `load web` with `GBASIC_PATH` pointing at the
library directory.

This is the library layer of PLAT-WEB: the routing the
[`server` block](plat-web-design-draft.md) would eventually be *sugar for*,
written as ordinary values. Routes are records in an array, matching is a
function over strings, and none of it needs a socket — which is why the whole
scheme is testable without one (`tests/run_web_routes.sh`).

It composes with `webserver` but does not depend on it: `web.dispatch` returns
an ordinary response record, which is exactly what `server.responses` accepts.

```basic
load web
load webserver

function home(req)
    return { body: "hello" }
end function

function product(req)
    return { body: "product " + req.params.id }
end function

routes = web.routes([
    { method: "get",  path: "/",              handler: home },
    { method: "get",  path: "/products/{id}", handler: product }
])

server = webserver.listen(8080)
watch(server.requests)
    while count(server.requests) > 0
        req = take_first(server.requests)
        append(server.responses, web.dispatch(routes, req))
    end while
end watch
```

## The table is checked when it is built

`web.routes` validates and **raises** — at startup, where a mistake is a crash
with a message naming the offending route, rather than at 3am as a 404 nobody
can explain. This is the library stand-in for the load-time errors the block
grammar is meant to provide.

Refused, each because the alternative is a plausible wrong answer rather than
an obvious failure:

| Written | Refused because |
|---|---|
| `method: "GTE"` | a typo'd verb would never match and 404 forever |
| `path: "products"` | a path that is not rooted can never equal a request path |
| `path: "/products/"` | a trailing slash is a different path (see below) |
| `path: "/a{b}"` | a half-written pattern would quietly become a static segment |
| `path: "/{id"` | unclosed pattern |
| `path: "/{rest...}/x"` | a greedy capture that is not last cannot be resolved |
| `path: "/{a}/{a}"` | the second capture would silently win the name |
| `path: "/{1st}"` | a capture name must be reachable as `req.params.<name>` |
| `path: "/a//b"` | an empty segment matches nothing |
| `/a/{x}` beside `/a/{y}` | indistinguishable — dispatch would have to guess |
| `handler: 5` | not callable |

## Patterns

A path is a series of `/`-separated segments. Each is one of:

| Segment | Matches | Capture |
|---|---|---|
| `products` | exactly that text | — |
| `{id}` | exactly one non-empty segment | `req.params.id` |
| `{rest...}` | one or more remaining segments, joined with `/` | `req.params.rest` |

`{name...}` may only be the final segment.

## Matching is order-independent

Specificity decides, not table position. Comparing two routes segment by
segment, the first place they differ is settled by

```
static  >  {param}  >  {rest...}
```

so `/products/new` beats `/products/{id}` however the table is written. Two
routes that differ **only** in their capture names are a tie, and a tie is a
build-time error rather than a runtime coin flip.

## What it deliberately does not do

Each of these would be a convenience that silently changes what a client asked
for:

- **No percent-decoding of captures.** `req.path` is raw, so `{id}` hands back
  exactly the bytes between the slashes. Decoding here would make `%2F`
  indistinguishable from a real separator — a path-traversal bug waiting for a
  handler that joins a capture onto a directory.
- **No trailing-slash normalisation.** `/cart` and `/cart/` are different paths
  and the second 404s. Rewriting one into the other is a policy — one that
  turns a POST into a redirect — and it belongs in the caller, one line before
  dispatch.
- **No HEAD from a GET route, and no automatic OPTIONS.** Declare them.

## API

**`web.routes(list)`** → the prepared table. `list` is an array of
`{ method, path, handler }` records; `method` is case-insensitive and must be
one of GET, POST, PUT, PATCH, DELETE, HEAD, OPTIONS; `handler` is a function
value. Raises on anything listed above.

**`web.resolve(table, method, path)`** → the pure matcher. No handler is
called and no response is built:

```basic
{ ok: true,  route: <the winning route>, params: { id: "42" }, allow: ["GET"] }
{ ok: false, params: {},                 allow: ["POST", "PUT"] }
```

`allow` lists the methods declared for a path that *did* match, which is what
separates "no such page" from "not that verb".

**`web.dispatch(table, req)`** → a complete response record. Captures arrive
as `req.params`. The handler's record is returned with `status` (200),
`headers` (`{}`) and `id` (from `req.id`) filled in where absent, so the value
describes itself without relying on a downstream default.

Answers the library produces itself:

| Situation | Response |
|---|---|
| no route matches the path | `404 Not Found` |
| the path matches, the method does not | `405 Method Not Allowed` with an `allow` header |
| the handler returns something that is not a record | `500`, and the route is named on standard error |

The 500 rather than a raise is deliberate *for now*: the design's error model
is let-it-crash under a supervisor, but there is no worker pool yet, so raising
would take the whole listener down over one bad handler.

**`web.static(relative, root)`** → a response record serving one file from
under `root`. `id` is left off, so `web.dispatch` fills it in.

```basic
function assets(req)
    return web.static(req.params.path, "public")
end function

routes = web.routes([
    { method: "get", path: "/assets/{path...}", handler: assets }
])
```

It takes the relative path rather than the request because gBASIC has no
closures: a handler cannot capture a root, so the root is named at the call
site where it is visible anyway.

**Canonicalize, then check — in that order, which is the whole point.** A
"does it start with the root" test applied to the path a *client sent* can be
walked out of with `..`, and cannot see a symlink at all. So the path is
resolved by the kernel first (`real_path`) and containment is tested on the
answer, on a separator boundary so a root of `pub` does not match
`pub-secret`.

| Situation | Response |
|---|---|
| a file under the root, including through a symlink that stays inside | `200`, content type from the extension |
| a path that resolves outside the root (`..`, or a symlink pointing out) | `403` |
| nothing there, a directory, a device, an empty path, a path with a NUL | `404` |
| the root itself is missing or is not a directory | `500`, named on standard error |

A directory is `404` rather than a listing: an index of what is on the disk is
a disclosure, and publishing one should be a decision someone made on purpose.

Unknown extensions are served as `application/octet-stream` rather than
guessed at — a wrong content type is how a text file becomes a script. The
root is served **as it is**, dotfiles included: hiding them would be a rule
with a famous exception (`.well-known/acme-challenge`, which TLS provisioning
needs), so put only public files under the root.

**Known limit:** the body is read whole into the response record, because the
response model has no streaming yet (that is Gap E in the lowering study). This
is fine for pages, styles and images; it is not a file server for large
downloads.

**`web.paths(table)`** → the table as sorted `"METHOD /path"` strings. The
static route list, available without running a request through anything.

## Tests

`tests/run_web_routes.sh` — semantics and oracle goldens (both fixtures state
their own expected answers), 13 refusal messages, and a live tier that puts the
table behind a real listener on loopback. The two invariant tiers are
complementary and were measured to be: order-independence catches a
first-match-wins router that the oracle misses, and the oracle catches an
inverted specificity rule that order-independence misses.
