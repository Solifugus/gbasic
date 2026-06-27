# gBASIC Site Sample App

This directory is the planned Postgres-backed sample application for the
eventual gBASIC home site. It is intentionally local-first while the webserver,
auth, and deployment story mature.

## Current Scope

The app currently includes the project shape and initial Postgres foundation:

- `site.bas`: loopback webserver entry point
- `site_postgres.bas`: Postgres-backed loopback webserver entry point
- `setup.bas`: initializes and seeds app tables in the configured Postgres database
- `sql/`: Postgres schema, seed, and reset scripts
- `static/`: vanilla CSS and JavaScript assets
- `deploy/`: example nginx, systemd, and environment-file templates
- `tests/`: app-specific client/test helpers

The static app serves a small local home page, a stylesheet, a tiny script, and
a shutdown route used by the test runner. The Postgres-backed app renders the
home, docs, about, examples, capped forum pages, and narrow local moderation
tools from Postgres.

## Run Locally

Build gBASIC from the repository root, then run:

```sh
./gbasic examples/gbasic_site/site.bas
```

The app writes the selected loopback port to `examples/gbasic_site/tmp_port.txt`.
Open `http://127.0.0.1:<port>/`.

The server binds to an operating-system-assigned loopback port through
gBASIC's current WebServer module. Do not expose this app publicly yet.

For deployment-style local runs, set a stable loopback port:

```sh
export GBASIC_SITE_PORT=8080
```

You can also use a local fallback file:

```sh
cp examples/gbasic_site/server_port.example.txt examples/gbasic_site/server_port.txt
```

`GBASIC_SITE_PORT` takes precedence when both are present. The test runners set
`GBASIC_SITE_PORT=0` so tests continue using ephemeral ports.

After Postgres setup, run the database-backed entry point:

```sh
./gbasic examples/gbasic_site/site_postgres.bas
```

It also writes the selected port to `examples/gbasic_site/tmp_port.txt`.

The admin page requires a signed-in admin user. Admin credentials are
provisioned at setup time (see "Postgres Setup" below) by running `setup.bas`
with `GBASIC_SITE_ADMIN_USER` and `GBASIC_SITE_ADMIN_PASSWORD` set. Passwords
are stored as salted hashes through `password_hash()`; the plaintext password
is never written to the database.

Open `/login`, sign in with the admin username and password to receive a
server-side session cookie, then open `/admin` to view moderation tools. There
is no longer a token-based admin path. Hide actions record moderation
timestamps and attribute the action to the authenticated username.

State-changing forms require a CSRF token. There are three independent CSRF
paths:

- **Admin moderation forms** use the per-session CSRF token stored with the
  server-side session in Postgres.
- **Anonymous posting forms, in production**, use a per-visitor cookie-bound
  double-submit token. When no shared token is configured, each visitor is
  issued a random token in an `HttpOnly; SameSite=Lax` cookie
  (`gbasic_site_anon_csrf`), and the form's hidden field must match that cookie
  on POST. This needs no server-side state and is the default posture.
- **Anonymous posting forms, in development/tests**, use a single shared token
  when `GBASIC_SITE_CSRF_TOKEN` (or the fallback file) is set. Setting it
  switches anonymous forms into shared-token mode, which is convenient for
  stateless test clients.

```sh
# Development shortcut: shared anonymous-form token.
export GBASIC_SITE_CSRF_TOKEN=change-this-local-csrf-token
```

You can also use a local fallback file instead of the variable:

```sh
cp examples/gbasic_site/csrf_token.example.txt examples/gbasic_site/csrf_token.txt
chmod 600 examples/gbasic_site/csrf_token.txt
```

For public deployment, leave `GBASIC_SITE_CSRF_TOKEN` unset so anonymous forms
use the cookie-bound token. The shared token is development-only.

## Anonymous Posting Rate Limit

Anonymous topic and reply posting can be rate limited per client IP. It is
disabled by default; enable it with:

```sh
export GBASIC_SITE_POST_RATE_LIMIT=5    # max accepted posts per window (0 = off)
export GBASIC_SITE_POST_RATE_WINDOW=60  # window length in seconds (default 60)
```

Accepted posts are recorded in `gbasic_site_post_events`; once an IP reaches the
limit within the window, further posts return `429` until the window passes. The
client IP is taken from the last `X-Forwarded-For` hop when present (for a single
trusted reverse proxy) and the direct socket address otherwise. This is spam
friction, not authentication.

## Tests

From the repository root:

```sh
./tests/run_gbasic_site.sh
```

The runner starts the app on an ephemeral loopback port, performs a few HTTP
checks, and shuts it down.

For the Postgres-backed app:

```sh
GBASIC_SITE_POSTGRES_TEST=1 ./tests/run_gbasic_site_postgres.sh
```

## Postgres Setup

The app uses normal libpq environment variables:

```sh
export PGHOST=127.0.0.1
export PGPORT=5432
export PGDATABASE=gbasic_site_dev
export PGUSER=gbasic_site
```

Use `~/.pgpass` or `PGPASSWORD` for credentials.

For a local development database on systems with `sudo -u postgres psql`, this
helper creates/updates a dedicated role and database and writes a matching
`~/.pgpass` entry:

```sh
examples/gbasic_site/scripts/setup_postgres_dev.sh
```

You can override defaults:

```sh
DB_NAME=gbasic_site_dev DB_USER=gbasic_site DB_HOST=127.0.0.1 DB_PORT=5432 \
    examples/gbasic_site/scripts/setup_postgres_dev.sh
```

Initialize/reset the app tables and seed data. Set the admin credentials in the
same command to provision (or update) an admin login:

```sh
GBASIC_SITE_ADMIN_USER=admin GBASIC_SITE_ADMIN_PASSWORD=change-this-password \
    ./gbasic examples/gbasic_site/setup.bas
```

This creates and resets only `gbasic_site_*` tables in the configured database,
including the auth/session tables. When `GBASIC_SITE_ADMIN_USER` and
`GBASIC_SITE_ADMIN_PASSWORD` are both set, it hashes the password with
`password_hash()` and upserts an enabled admin user. Without them, setup runs
normally but provisions no admin account.

## Backups

Create a local custom-format Postgres dump:

```sh
examples/gbasic_site/scripts/backup_postgres.sh
```

Restore requires an explicit confirmation string because it can replace app
tables:

```sh
export GBASIC_SITE_RESTORE_CONFIRM=restore-gbasic_site_dev
examples/gbasic_site/scripts/restore_postgres.sh examples/gbasic_site/backups/<backup>.dump
```

## Deployment Notes

Do not publish the app directly yet. The current deployment inventory is in
`docs/gbasic_site_deployment.md`; it assumes a loopback gBASIC app behind
nginx and calls out the auth, CSRF, spam-prevention, logging, and backup work
needed before public forum use. The target authentication/session model is in
`docs/gbasic_site_auth_plan.md`.

Deployment templates live in `examples/gbasic_site/deploy/`:

- `gbasic-site.nginx.conf`
- `gbasic-site.service`
- `site.env.example`
