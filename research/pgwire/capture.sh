#!/usr/bin/env bash
# Records what real PostgreSQL clients send, through proxy.py, into corpus/
# (docs/gsql_pgwire_design.md §2). Research code: not run by the gate.
#
# Needs a local PostgreSQL reachable by peer authentication on its unix socket
# (the proxy listens on TCP and forwards there, so no password is involved),
# psql, python3, node/npm, go and java 11+. The drivers are installed into
# WORK, never into the tree:
#   research/pgwire/capture.sh WORK
# The corpus is anonymised (the login and the time zone replaced) before it is
# written. A database `pgcap` is created and filled if it is missing.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
work="${1:?usage: capture.sh WORK-DIRECTORY}"
mkdir -p "$work"
gbasic="$here/../../gbasic"

if ! psql -d pgcap -c 'select 1' >/dev/null 2>&1; then
    createdb pgcap
    psql -d pgcap -q <<'SQL'
create table customer (id serial primary key, name text not null, city text, since date);
create table invoice (id serial primary key, customer_id int references customer(id), total numeric(12,2), issued timestamp);
create index invoice_cust on invoice(customer_id);
create view big as select * from invoice where total > 100;
insert into customer(name,city,since) values ('Ann','Leeds','2020-01-02'),('Bo','York',null);
insert into invoice(customer_id,total,issued) values (1,150.5,'2026-01-01 10:00'),(2,20,'2026-02-01');
SQL
fi

# Driver versions as captured 2026-11-19: psycopg 3 and psycopg2 (latest on
# PyPI), SQLAlchemy and pandas (latest), node-postgres (latest), pgx v5.7.1,
# pgJDBC 42.7.4.
[ -x "$work/venv/bin/python" ] || { python3 -m venv "$work/venv"; "$work/venv/bin/pip" -q install 'psycopg[binary]' psycopg2-binary sqlalchemy pandas; }
[ -d "$work/node/node_modules/pg" ] || { mkdir -p "$work/node"; (cd "$work/node" && npm init -y >/dev/null && npm i -s pg); }
cp "$here/clients/node_pg.js" "$work/node/client.js"
if [ ! -x "$work/gopgx/pgxcap" ]; then
    mkdir -p "$work/gopgx"; cp "$here/clients/pgx_main.go" "$work/gopgx/main.go"
    (cd "$work/gopgx" && { [ -f go.mod ] || go mod init pgcap >/dev/null 2>&1; } && go get github.com/jackc/pgx/v5@v5.7.1 && go build -o pgxcap .)
fi
[ -f "$work/pgjdbc.jar" ] || curl -sSfL -o "$work/pgjdbc.jar" https://repo1.maven.org/maven2/org/postgresql/postgresql/42.7.4/postgresql-42.7.4.jar

logs="$work/logs"; mkdir -p "$logs"; rm -f "$logs"/*.jsonl
port=15432
run() {
    local name=$1; shift
    python3 "$here/proxy.py" "$port" "$logs/$name.jsonl" & local pp=$!
    sleep 0.5
    PGCAP_PORT=$port "$@" >"$logs/$name.stdout" 2>&1 || { echo "$name failed:"; cat "$logs/$name.stdout"; }
    sleep 0.3; kill "$pp"; wait "$pp" 2>/dev/null || true
    echo "captured $name"
    port=$((port + 1))
}
dsn() { echo "host=127.0.0.1 port=$port dbname=pgcap sslmode=disable"; }
run psql_connect bash -c 'psql "$0" -c "select 1"' "$(dsn)"
run psql_meta bash -c 'psql "$0" -c "\l" -c "\dn" -c "\dt" -c "\d customer" -c "\dv" -c "\di" -c "\df" -c "\du"' "$(dsn)"
for c in psycopg3 psycopg2 sqlalchemy pandas; do run $c "$work/venv/bin/python" "$here/clients/py_clients.py" "$port" $c; done
run node_pg bash -c 'node "$0/node/client.js" "$PGCAP_PORT"' "$work"
run go_pgx bash -c '"$0/gopgx/pgxcap" "$PGCAP_PORT"' "$work"
run jdbc bash -c 'java -cp "$0/pgjdbc.jar" "$1" "$PGCAP_PORT"' "$work" "$here/clients/Cap.java"
run jdbc_meta bash -c 'java -cp "$0/pgjdbc.jar" "$1" "$PGCAP_PORT" meta' "$work" "$here/clients/Cap.java"
run gbasic_pg "$gbasic" "$here/clients/gbasic_pg.bas"

me="$(id -un)"
for f in "$logs"/*.jsonl; do
    sed -e "s/$me/gbasic/g" -e 's#"TimeZone": "[^"]*"#"TimeZone": "UTC"#' -e 's#\["TimeZone", "[^"]*"\]#["TimeZone", "UTC"]#' "$f" >"$here/corpus/$(basename "$f")"
done
python3 "$here/summarize.py" "$here/corpus"
