# gBASIC Site Sample App

This directory is the planned Postgres-backed sample application for the
eventual gBASIC home site. It is intentionally local-first while the webserver,
auth, and deployment story mature.

## Current Scope

Phase 0 only establishes the project shape:

- `site.bas`: loopback webserver entry point
- `sql/`: future Postgres schema, seed, and reset scripts
- `static/`: vanilla CSS and JavaScript assets
- `tests/`: app-specific client/test helpers

The app currently serves a small local home page, a stylesheet, a tiny script,
and a shutdown route used by the test runner. Postgres-backed pages and forum
data come in later phases.

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

## Future Postgres Setup

The app will use normal libpq environment variables when database-backed
features are added, such as `PGHOST`, `PGPORT`, `PGDATABASE`, `PGUSER`, and
`PGPASSWORD`.
