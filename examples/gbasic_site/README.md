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

The local admin page requires a token. Prefer an environment variable:

```sh
export GBASIC_SITE_ADMIN_TOKEN=change-this-local-admin-token
```

You can also use a local fallback file:

```sh
cp examples/gbasic_site/admin_token.example.txt examples/gbasic_site/admin_token.txt
chmod 600 examples/gbasic_site/admin_token.txt
```

State-changing forms also require a CSRF token. Prefer an environment
variable:

```sh
export GBASIC_SITE_CSRF_TOKEN=change-this-local-csrf-token
```

You can also use a local fallback file:

```sh
cp examples/gbasic_site/csrf_token.example.txt examples/gbasic_site/csrf_token.txt
chmod 600 examples/gbasic_site/csrf_token.txt
```

Open `/login` to exchange the temporary local admin token for a session cookie,
then open `/admin` to view moderation tools. The older `/admin?token=<token>`
flow still works for local development while the real password-auth flow is
being built. Hide actions record moderation timestamps and a local moderator
label, but not the token itself.

Session-backed admin forms use the CSRF token stored with the session. Public
posting forms and the older `/admin?token=<token>` path still use the shared
development CSRF token. These token-backed approaches are intentionally
temporary until gBASIC has password hashing.

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

Initialize/reset the app tables and seed data:

```sh
./gbasic examples/gbasic_site/setup.bas
```

This creates and resets only `gbasic_site_*` tables in the configured database,
including the auth/session tables used by the upcoming login flow.

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
