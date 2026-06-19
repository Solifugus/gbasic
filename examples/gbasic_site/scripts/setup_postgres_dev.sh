#!/usr/bin/env bash
set -euo pipefail

DB_NAME="${DB_NAME:-gbasic_site_dev}"
DB_USER="${DB_USER:-gbasic_site}"
DB_HOST="${DB_HOST:-127.0.0.1}"
DB_PORT="${DB_PORT:-5432}"
POSTGRES_ADMIN="${POSTGRES_ADMIN:-postgres}"

read -r -s -p "Password for PostgreSQL role ${DB_USER}: " DB_PASS
printf '\n'

sql_escape() {
    printf "%s" "$1" | sed "s/'/''/g"
}

db_name_sql="$(sql_escape "$DB_NAME")"
db_user_sql="$(sql_escape "$DB_USER")"
db_pass_sql="$(sql_escape "$DB_PASS")"

sudo -u "$POSTGRES_ADMIN" psql -v ON_ERROR_STOP=1 \
    -v db_name="$db_name_sql" \
    -v db_user="$db_user_sql" \
    -v db_pass="$db_pass_sql" <<'SQL'
DO $$
DECLARE
    role_name text := :'db_user';
    role_password text := :'db_pass';
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_roles WHERE rolname = role_name
    ) THEN
        EXECUTE format('CREATE ROLE %I LOGIN PASSWORD %L', role_name, role_password);
    ELSE
        EXECUTE format('ALTER ROLE %I WITH LOGIN PASSWORD %L', role_name, role_password);
    END IF;
END
$$;

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
