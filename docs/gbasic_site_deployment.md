# gBASIC Site Deployment and Hardening Notes

The sample site is still local-first. These notes describe the intended shape
for a future public deployment and the work that must be done before exposing
the forum or admin paths on the internet.

## Serving Model

Run the gBASIC app on loopback and put nginx or Caddy in front of it.

- Terminate TLS in nginx or Caddy.
- Proxy dynamic requests to the gBASIC loopback listener.
- Serve immutable static files from the reverse proxy when possible.
- Keep Postgres on localhost or a private network.
- Do not bind the current gBASIC webserver directly to a public interface.

Caddy is the smaller operational default because it handles certificates with
less configuration. nginx is still a reasonable choice if the server already
uses it.

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

The current app validates required fields, but it does not yet enforce size
limits.

## Logging

Public deployment needs structured enough logs to debug abuse and failures:

- request method, path, status, duration, and remote address from the proxy,
- application errors from the gBASIC process,
- Postgres errors,
- moderation actions with topic/post id and moderator label,
- startup/shutdown events.

The first version can rely on reverse-proxy access logs plus captured stdout
and stderr from the gBASIC process.

## Backups

Postgres is the durable state. A public deployment needs:

- scheduled `pg_dump` backups,
- off-server backup copies,
- documented restore commands,
- periodic restore tests,
- a retention policy.

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

Captcha can be considered later, but it should not be the only control.

## Auth, Sessions, and CSRF

The local admin token file is only a dogfooding shortcut. Public deployment
needs real authentication before write/admin paths are exposed:

- password hashing with a modern password hash,
- server-side sessions or signed session cookies,
- secure, HttpOnly, SameSite cookies,
- CSRF tokens on all state-changing form submissions,
- logout and session rotation,
- a way to revoke compromised credentials.

The current admin token should never be treated as a production auth system.

## First Deployment Checklist

Before publishing on the public-IP server:

- create a dedicated Unix user for the app,
- create a dedicated Postgres role and database,
- run the setup program only against the intended database,
- create an admin token only if the admin path is still local-only,
- run the gBASIC app as a supervised service,
- bind the app to loopback,
- configure Caddy or nginx with TLS,
- proxy dynamic traffic to the loopback app,
- configure request-size limits in the proxy,
- enable proxy access logs and app stderr/stdout capture,
- configure scheduled Postgres backups,
- verify restore from a backup,
- keep `/admin` unreachable publicly until auth/session/CSRF work is done,
- run the integration tests against a staging database before updating the
  public service.
