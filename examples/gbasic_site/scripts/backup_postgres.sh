#!/usr/bin/env bash
set -euo pipefail

DB_NAME="${DB_NAME:-${PGDATABASE:-gbasic_site_dev}}"
DB_USER="${DB_USER:-${PGUSER:-gbasic_site}}"
DB_HOST="${DB_HOST:-${PGHOST:-127.0.0.1}}"
DB_PORT="${DB_PORT:-${PGPORT:-5432}}"
BACKUP_DIR="${BACKUP_DIR:-examples/gbasic_site/backups}"

mkdir -p "$BACKUP_DIR"

timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
backup_file="${BACKUP_DIR}/${DB_NAME}-${timestamp}.dump"

pg_dump \
    --format=custom \
    --no-owner \
    --no-privileges \
    --table='gbasic_site_*' \
    --host="$DB_HOST" \
    --port="$DB_PORT" \
    --username="$DB_USER" \
    --dbname="$DB_NAME" \
    --file="$backup_file"

printf 'Wrote %s\n' "$backup_file"
