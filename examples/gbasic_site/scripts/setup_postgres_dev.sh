#!/usr/bin/env bash
set -euo pipefail

DB_NAME="${DB_NAME:-gbasic_site_dev}"
DB_USER="${DB_USER:-gbasic_site}"
DB_HOST="${DB_HOST:-127.0.0.1}"
DB_PORT="${DB_PORT:-5432}"
POSTGRES_ADMIN="${POSTGRES_ADMIN:-postgres}"

read -r -s -p "Password for PostgreSQL role ${DB_USER}: " DB_PASS
printf '\n'

sudo -u "$POSTGRES_ADMIN" psql -v ON_ERROR_STOP=1 \
    -v db_name="$DB_NAME" \
    -v db_user="$DB_USER" \
    -v db_pass="$DB_PASS" <<'SQL'
SELECT format('CREATE ROLE %I LOGIN PASSWORD %L', :'db_user', :'db_pass')
WHERE NOT EXISTS (
    SELECT 1 FROM pg_roles WHERE rolname = :'db_user'
)\gexec

SELECT format('ALTER ROLE %I WITH LOGIN PASSWORD %L', :'db_user', :'db_pass')
WHERE EXISTS (
    SELECT 1 FROM pg_roles WHERE rolname = :'db_user'
)\gexec

SELECT format('CREATE DATABASE %I OWNER %I', :'db_name', :'db_user')
WHERE NOT EXISTS (
    SELECT 1 FROM pg_database WHERE datname = :'db_name'
)\gexec
SQL

touch "$HOME/.pgpass"
chmod 600 "$HOME/.pgpass"

tmp_pgpass="$(mktemp)"
grep -v -F "${DB_HOST}:${DB_PORT}:${DB_NAME}:${DB_USER}:" "$HOME/.pgpass" >"$tmp_pgpass" || true
printf '%s:%s:%s:%s:%s\n' "$DB_HOST" "$DB_PORT" "$DB_NAME" "$DB_USER" "$DB_PASS" >>"$tmp_pgpass"
mv "$tmp_pgpass" "$HOME/.pgpass"
chmod 600 "$HOME/.pgpass"

cat <<EOF

Postgres dev database is ready.

Use these environment variables before running gBASIC site setup/tests:

export PGHOST=${DB_HOST}
export PGPORT=${DB_PORT}
export PGDATABASE=${DB_NAME}
export PGUSER=${DB_USER}

Initialize app tables and seed data:

./gbasic examples/gbasic_site/setup.bas

Run the Postgres integration check:

GBASIC_SITE_POSTGRES_TEST=1 ./tests/run_gbasic_site_postgres.sh
EOF
