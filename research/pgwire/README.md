# What PostgreSQL clients send

The measurement behind `docs/gsql_pgwire_design.md` §2. **Research code:**
nothing here is linked into `gbasic`, and nothing here is run by
`tests/run_all.sh`.

```sh
research/pgwire/capture.sh WORK          # capture every client again (drivers installed into WORK)
python3 research/pgwire/summarize.py     # the design's §2 table, from corpus/
```

| file | what |
|---|---|
| `proxy.py` | a logging proxy: TCP on loopback, forwarded to the local PostgreSQL's unix socket; one JSON line per message |
| `capture.sh` | creates the `pgcap` database if missing, installs the drivers into WORK, runs each client through the proxy, anonymises the logs into `corpus/` |
| `clients/` | one program per client: psycopg 3, psycopg2, SQLAlchemy, pandas (`py_clients.py`), node-postgres, pgx, pgJDBC, gBASIC's `pg` |
| `corpus/` | the anonymised logs of 2026-11-19 (login replaced by `gbasic`, time zone by `UTC`) |
| `summarize.py` | per client: statements, catalog queries, binary formats, PostgreSQL-only syntax; then the catalog relations and functions used |

The corpus is meant to become a test's input: increment 2 of the design
replays its frontend messages against the front end and holds the backend
message types to the recording.
