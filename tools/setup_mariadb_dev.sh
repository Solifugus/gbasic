#!/usr/bin/env bash
# One-time developer setup so the ODBC suites can be verified against a REAL
# MariaDB on this machine, rather than only against the hermetic SQLite driver.
#
#   sudo tools/setup_mariadb_dev.sh                  # password defaults to gbasic
#   sudo tools/setup_mariadb_dev.sh somepassword     # or name one
#
# WHY THIS EXISTS. run_odbc was verified against MariaDB 11.8 and SQL Server
# 2025 on 2026-08-29, and that run found THREE defects invisible to SQLite --
# booleans bound as the character "1" that a real BIT column refuses, booleans
# READ as text so a column holding true came back FALSE with no error at all,
# and parameters declared SQL_VARCHAR so non-Latin-1 text VANISHED into SQL
# Server. None of that is reachable from SQLite, which is dynamically typed.
#
# The credentials for that run were never written down, so the next session
# (2026-09-08, adding the schema catalog) could not repeat it and had to record
# "verified hermetically only, which is weak evidence". Postgres has had
# tools/setup_postgres_dev.sh for exactly this reason. This is its counterpart.
#
# What it does, and nothing else:
#   - creates the MariaDB user `gbasic`@`localhost` with the given password
#   - creates the database `gbasic_test` and grants that user rights ON IT ONLY
#
# Idempotent. It touches no configuration file and no database it did not
# create, and it grants nothing outside gbasic_test.
#
# Afterwards, as the developer:
#   GBASIC_ODBC_DRIVER='MariaDB Unicode' \
#   GBASIC_ODBC_CONNECTION='Driver=MariaDB Unicode;Server=127.0.0.1;Port=3306;UID=gbasic;PWD=gbasic;Database=gbasic_test' \
#     ./tests/run_odbc.sh
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "run with sudo: it needs the MariaDB root socket login" >&2
    exit 1
fi

password="${1:-gbasic}"
client=$(command -v mariadb || command -v mysql || true)
if [ -z "$client" ]; then
    echo "no mariadb/mysql client found" >&2
    exit 1
fi

if ! "$client" -e 'select 1' >/dev/null 2>&1; then
    echo "cannot reach MariaDB as root over the local socket." >&2
    echo "Is the server running?  systemctl status mariadb" >&2
    exit 1
fi

"$client" <<SQL
create database if not exists gbasic_test
    character set utf8mb4 collate utf8mb4_unicode_ci;
create user if not exists 'gbasic'@'localhost' identified by '$password';
alter user 'gbasic'@'localhost' identified by '$password';
grant all privileges on gbasic_test.* to 'gbasic'@'localhost';
flush privileges;
SQL

echo "ok: database gbasic_test, user gbasic@localhost"
echo
echo "verify the ODBC suites against it with:"
echo
echo "  GBASIC_ODBC_DRIVER='MariaDB Unicode' \\"
echo "  GBASIC_ODBC_CONNECTION='Driver=MariaDB Unicode;Server=127.0.0.1;Port=3306;UID=gbasic;PWD=$password;Database=gbasic_test' \\"
echo "    ./tests/run_odbc.sh"
