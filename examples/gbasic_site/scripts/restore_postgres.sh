#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    printf 'usage: %s BACKUP.dump\n' "$0" >&2
    exit 2
fi

if [[ "${GBASIC_SITE_RESTORE_CONFIRM:-}" != "restore-${DB_NAME:-${PGDATABASE:-gbasic_site_dev}}" ]]; then
    cat >&2 <<EOF
Refusing to restore without confirmation.

Set:
  export GBASIC_SITE_RESTORE_CONFIRM=restore-${DB_NAME:-${PGDATABASE:-gbasic_site_dev}}

Then rerun:
  $0 $1
EOF
    exit 2
fi

backup_file="$1"
if [[ ! -f "$backup_file" ]]; then
    printf 'backup file not found: %s\n' "$backup_file" >&2
    exit 1
fi

DB_NAME="${DB_NAME:-${PGDATABASE:-gbasic_site_dev}}"
DB_USER="${DB_USER:-${PGUSER:-gbasic_site}}"
DB_HOST="${DB_HOST:-${PGHOST:-127.0.0.1}}"
DB_PORT="${DB_PORT:-${PGPORT:-5432}}"

pg_restore \
    --clean \
    --exit-on-error \
    --if-exists \
    --no-owner \
    --no-privileges \
    --host="$DB_HOST" \
    --port="$DB_PORT" \
    --username="$DB_USER" \
    --dbname="$DB_NAME" \
    "$backup_file"

printf 'Restored %s into %s\n' "$backup_file" "$DB_NAME"
