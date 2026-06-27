# gBASIC Site Deployment and Hardening Notes

The sample site is still local-first. These notes describe the intended shape
for a future public deployment and the work that must be done before exposing
the forum or admin paths on the internet.

## Serving Model

Run the gBASIC app on loopback and put nginx in front of it.

- Terminate TLS in nginx.
- Proxy dynamic requests to the gBASIC loopback listener.
- Serve immutable static files from the reverse proxy when possible.
- Keep Postgres on localhost or a private network.
- Do not bind the current gBASIC webserver directly to a public interface.

nginx is the default deployment target for this project because it matches the
server stack already in use. Caddy remains a reasonable alternative if a future
deployment wants automatic certificate management with less configuration.

## nginx Sketch

The exact paths and domain will change, but the first public shape is captured
in `examples/gbasic_site/deploy/gbasic-site.nginx.conf`.

That example terminates TLS, serves `/static/` directly, proxies dynamic
traffic to `127.0.0.1:8080`, and deliberately blocks `/admin`.

This deliberately blocks `/admin` at nginx until the app has real sessions,
CSRF protection, and production-grade auth.

## Stable Loopback Port

Local tests use port `0` so the operating system can choose an ephemeral port.
nginx and systemd need a stable target. For deployment-style runs, set:

```sh
export GBASIC_SITE_PORT=8080
```

or create a fallback file:

```sh
cp examples/gbasic_site/server_port.example.txt examples/gbasic_site/server_port.txt
```

The example file uses `8080`, matching the nginx template. `GBASIC_SITE_PORT`
takes precedence when both are present. The app still binds to loopback through
the WebServer module. Test runners set `GBASIC_SITE_PORT=0` so they continue
to use ephemeral ports.

## systemd Sketch

Example service and environment files live at:

- `examples/gbasic_site/deploy/gbasic-site.service`
- `examples/gbasic_site/deploy/site.env.example`

Before installing them, adjust the Unix user, repository path, database name,
and service environment location for the target server. The environment example
uses `PGPASSFILE=/etc/gbasic-site/pgpass`; keep that file, or any file that
contains a database password, readable only by trusted users.

## Bind Address

The current WebServer module intentionally listens on loopback. Public binding
should stay explicit in gBASIC so a local development app cannot accidentally
become internet-facing.

Before direct public serving is considered, gBASIC needs:

- explicit bind-address support,
- clear defaults that prefer loopback,
- tests that prove loopback-only behavior,
- documentation that distinguishes local and public listeners.

## Request Limits

The forum write paths need limits before public use:

- maximum request body size,
- maximum title, name, and post body lengths,
- request read timeouts,
- response/write timeouts,
- clear `413` or `400` responses for oversized or malformed submissions.

The app currently enforces field length limits for forum names, titles, and
post bodies. A reverse proxy `client_max_body_size` limit is still needed to
reject oversized requests before the app reads them.

## Logging

Public deployment needs structured enough logs to debug abuse and failures:

- request method, path, status, duration, and remote address from the proxy,
- application errors from the gBASIC process,
- Postgres errors,
- moderation actions with topic/post id and moderator label,
- startup/shutdown events.

The app emits startup, shutdown, and simple request/status lines to stdout.
The first deployment can combine those with nginx access logs and captured
stderr from the gBASIC process.

## Backups

Postgres is the durable state. A public deployment needs:

- scheduled `pg_dump` backups,
- off-server backup copies,
- documented restore commands,
- periodic restore tests,
- a retention policy.

The sample app includes `examples/gbasic_site/scripts/backup_postgres.sh` and
`examples/gbasic_site/scripts/restore_postgres.sh`. They use the same `DB_*`
or libpq `PG*` variables as setup. Keep generated dumps outside the repository
or in the ignored `examples/gbasic_site/backups/` directory, then copy them
off-server.

The app setup script resets development tables and must not be used against a
production database.

## Spam Prevention

The public forum should not open anonymous posting without friction. Reasonable
first options include:

- invite-only posting,
- moderator-approved first posts,
- rate limits at the reverse proxy or app layer,
- temporary posting lockouts by IP,
- hidden-by-default moderation queue.

The app now ships an app-layer per-IP rate limit for anonymous topic and reply
posting. It is disabled by default; set `GBASIC_SITE_POST_RATE_LIMIT` to the
maximum accepted posts per window and `GBASIC_SITE_POST_RATE_WINDOW` to the
window length in seconds (default 60). Accepted posts are recorded in
`gbasic_site_post_events`; the third post past the limit within the window
returns `429`. The limit is keyed on the client IP, taken from the last
`X-Forwarded-For` hop when present (so it works behind a single trusted reverse
proxy) and the direct socket address otherwise.

For this to be correct behind nginx, the proxy MUST set `X-Forwarded-For` with
`proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;` and must not
forward a client-supplied `X-Forwarded-For` it did not append — otherwise the
last hop can be spoofed. The `gbasic_site_post_events` table grows by one row
per accepted post; prune it on a schedule (the rate check only reads a bounded
recent window, so old rows are dead weight).

Captcha can be considered later, but it should not be the only control. A
hidden-by-default moderation queue remains a reasonable stronger control to
layer on top of the rate limit.

## Auth, Sessions, and CSRF

Admin auth is now real: password hashing (`password_hash`/`password_verify`),
server-side sessions in Postgres, `HttpOnly; SameSite=Lax` session cookies,
per-session CSRF, login session rotation, and logout/revocation. Anonymous
posting forms use a per-visitor cookie-bound double-submit CSRF token by
default (the shared `GBASIC_SITE_CSRF_TOKEN` is a dev/test-only shortcut). For
public deployment:

- leave `GBASIC_SITE_CSRF_TOKEN` unset so anonymous forms use the cookie-bound
  token,
- keep admin write/moderation paths behind the session login,
- serve over HTTPS so the `Secure` cookie attribute is meaningful.

The target auth/session model is tracked in `docs/gbasic_site_auth_plan.md`.

## First Deployment Checklist

Before publishing on the public-IP server:

- create a dedicated Unix user for the app,
- create a dedicated Postgres role and database,
- run the setup program only against the intended database,
- leave `GBASIC_SITE_CSRF_TOKEN` unset in production (anonymous forms then use
  the per-visitor cookie-bound CSRF token); set it only for local/dev or test,
- set `GBASIC_SITE_PORT` or create `examples/gbasic_site/server_port.txt` with
  the loopback port used by nginx,
- run the gBASIC app as a supervised service,
- bind the app to loopback,
- configure nginx with TLS,
- proxy dynamic traffic to the loopback app,
- configure request-size limits in the proxy,
- enable proxy access logs and app stderr/stdout capture,
- configure scheduled Postgres backups,
- verify restore from a backup,
- keep `/admin` unreachable publicly until auth/session/CSRF work is done,
- run the integration tests against a staging database before updating the
  public service.
