# gBASIC Site Sample App

This directory is the planned Postgres-backed sample application for the
eventual gBASIC home site. It is intentionally local-first while the webserver,
auth, and deployment story mature.

## Current Scope

The app currently includes the project shape and initial Postgres foundation:

- `site.bas`: loopback webserver entry point
- `setup.bas`: initializes and seeds app tables in the configured Postgres database
- `sql/`: Postgres schema, seed, and reset scripts
- `static/`: vanilla CSS and JavaScript assets
- `tests/`: app-specific client/test helpers

The app currently serves a small local home page, a stylesheet, a tiny script,
and a shutdown route used by the test runner. Runtime pages still use static
content; reading pages and forum data from Postgres comes in later phases.

## Run Locally

Build gBASIC from the repository root, then run:

```sh
./gbasic examples/gbasic_site/site.bas
```

The app writes the selected loopback port to `examples/gbasic_site/tmp_port.txt`.
Open `http://127.0.0.1:<port>/`.

The server binds to an operating-system-assigned loopback port through
gBASIC's current WebServer module. Do not expose this app publicly yet.

## Tests

From the repository root:

```sh
./tests/run_gbasic_site.sh
```

The runner starts the app on an ephemeral loopback port, performs a few HTTP
checks, and shuts it down.

## Postgres Setup

The app uses normal libpq environment variables:

```sh
export PGHOST=127.0.0.1
export PGPORT=5432
export PGDATABASE=gbasic_site_dev
export PGUSER=gbasic_site
```

Use `~/.pgpass` or `PGPASSWORD` for credentials.

Initialize/reset the app tables and seed data:

```sh
./gbasic examples/gbasic_site/setup.bas
```

This creates and resets only `gbasic_site_*` tables in the configured database.
