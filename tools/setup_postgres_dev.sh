#!/usr/bin/env bash
# One-time developer setup so the opt-in PostgreSQL suites can run on this
# machine, and so a platform question about `pg` can be answered by running it
# rather than reading it.
#
#   sudo tools/setup_postgres_dev.sh            # as the developer's own login
#   sudo tools/setup_postgres_dev.sh alice      # or name the login explicitly
#
# What it does, and nothing else:
#   - creates a Postgres ROLE named after the Unix login, with LOGIN and
#     CREATEDB, so the stock peer-auth line in pg_hba.conf lets that user in
#     over the local socket with no password and no edits to pg_hba.conf
#   - creates two databases owned by that role:
#       gbasic_test      for tests/run_postgres.sh and run_gbasic_site_postgres.sh
#       gbasic_scratch   for one-off investigations (tests never touch it)
#   - enables the `vector` extension (pgvector) in gbasic_scratch if it is
#     installed, and says so either way
#
# Idempotent: every step checks before it acts. Nothing here touches
# postgresql.conf, pg_hba.conf, or any database it did not create.
#
# Afterwards, as the developer:
#   GBASIC_POSTGRES_TEST=1 PGDATABASE=gbasic_test ./tests/run_postgres.sh
#   GBASIC_SITE_POSTGRES_TEST=1 PGDATABASE=gbasic_test PGUSER=$USER ./tests/run_gbasic_site_postgres.sh
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "run with sudo: it needs to act as the postgres superuser" >&2
    exit 1
fi

login="${1:-${SUDO_USER:-}}"
if [ -z "$login" ]; then
    echo "could not tell which login to provision; pass it: sudo $0 alice" >&2
    exit 1
fi
if ! id "$login" >/dev/null 2>&1; then
    echo "no such Unix user: $login" >&2
    exit 1
fi

as_pg() { sudo -u postgres psql -v ON_ERROR_STOP=1 -qAt "$@"; }

if ! as_pg -c "select 1" >/dev/null 2>&1; then
    echo "cannot reach PostgreSQL as the postgres user -- is the server running?" >&2
    exit 1
fi

echo "== role $login"
if [ "$(as_pg -c "select 1 from pg_roles where rolname = '$login'")" = "1" ]; then
    echo "   exists"
else
    as_pg -c "create role \"$login\" login createdb"
    echo "   created (LOGIN, CREATEDB; peer auth over the local socket)"
fi

for db in gbasic_test gbasic_scratch; do
    echo "== database $db"
    if [ "$(as_pg -c "select 1 from pg_database where datname = '$db'")" = "1" ]; then
        echo "   exists"
    else
        as_pg -c "create database $db owner \"$login\""
        echo "   created, owned by $login"
    fi
done

echo "== pgvector in gbasic_scratch"
if [ "$(as_pg -c "select 1 from pg_available_extensions where name = 'vector'")" = "1" ]; then
    as_pg -d gbasic_scratch -c "create extension if not exists vector"
    echo "   enabled ($(as_pg -d gbasic_scratch -c "select extversion from pg_extension where extname = 'vector'"))"
else
    echo "   not installed on this server (apt: postgresql-$(as_pg -c 'show server_version_num' | cut -c1-2)-pgvector); the float8[] half of the investigation still runs"
fi

echo
echo "done. as $login:"
echo "  psql -d gbasic_scratch -c 'select current_user'"
echo "  GBASIC_POSTGRES_TEST=1 PGDATABASE=gbasic_test ./tests/run_postgres.sh"
