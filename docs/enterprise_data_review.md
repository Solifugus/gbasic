# Enterprise data work: what gBASIC has, what it lacks

**Status:** Ruled 2026-11-18 (Matthew: "ok.. right. good point"). §5's
order stands: bulk load (G2) in 0.13.0; the PostgreSQL wire protocol as a
design note after parallel readers; the discovery estate parked for an
outside author. Q5, message queues, is still open. Matthew, 2026-11-18, of
cross-database work: "I am not sure the shape of this but we should at least
go over it and make sure gbasic is facilitated for real enterprise data
work." This review also takes in two earlier items:

- whether G-SQL should speak PostgreSQL's wire protocol;
- the discovery library's test estate.

## 1. The work, and what gBASIC has at each stage

An enterprise data job usually runs through six stages. What exists at each,
as of 0.12.0:

| stage | what it is | gBASIC today |
|---|---|---|
| **reach** | connect to the systems that hold the data | `pg`, `odbc` (any ODBC database: SQL Server, Oracle, DB2, MariaDB...), `sqlite`, `gsql`, `ldap`, `webclient`/`http`, `net` with TLS, `smtp`, `mail.parse`, `process` for anything with a CLI. SFTP and SSH live in Transward |
| **land** | files arrive and are taken exactly once | `intake` (a ledger of what arrived and was taken, crash-safe), `glob`, streaming `for each line`, `csv.records`, `read_at` for large files, `zip`, `compress`/`uncompress` (zlib, raw, gzip) |
| **read** | turn what arrived into rows | `csv`, `xlsx` (reads and **edits** real workbooks), `xml` (streaming), `pdf` with OCR, `ari` (printed reports), `finio` (bank formats: NACHA, BAI2, camt.053, OFX, pain.001), `edgar`, `json`/`try_decode`, `notation` |
| **shape** | clean, type, reconcile | exact decimals and `money`, dates and business calendars, `frame`, `grid`, `consolidate`, `rules`, `stats`, three-valued logic, typed variables and columns |
| **load** | write it somewhere | `dbframe` (into sqlite or G-SQL, each refused row reported), `gsql.insert` (many rows, all or nothing), parameters on every driver, `xlsx` writing, `acctexport` |
| **serve and govern** | reports, APIs, lineage, audit | `webserver`/`server` blocks, `mcp`, `chart`, `discovery` (catalog and column lineage), `nlq`, `with principal`, crypto and signed tokens |

**The strength is the middle.** Exact money and decimals, dates, workbooks
edited rather than flattened, and every refusal reported rather than dropped:
that is what most ETL tools do badly, and what the finance libraries were
built on.

## 2. The gaps, ranked by how often a real job meets them

**G1. Other tools cannot read G-SQL.** A G-SQL database is reachable only
from gBASIC. None of these can read it:

- a BI tool (Power BI, Tableau, Metabase);
- Excel;
- Python;
- `psql`.

Enterprises judge a database by whether their tools reach it. Two routes:

- **PostgreSQL's wire protocol** (proposed in `docs/networking_design.md`
  §10). Every BI tool, every language and `psql` speak it. §3 prices it.
- An ODBC driver for G-SQL. Windows BI tools prefer ODBC, but a driver is a
  shared library per platform, loaded into someone else's process, and a far
  larger surface than one protocol.

**G2. No bulk load into Postgres or ODBC databases.** `pg` and `odbc` insert
a row per statement. There is no `COPY` (`PQputCopyData`), and ODBC
parameter arrays (`SQL_ATTR_PARAMSET_SIZE`) are not used, both checked in
the source on 2026-11-18. Loading 10⁶ rows into SQL Server is 10⁶ round
trips. This is the most ordinary enterprise job there is, and the fix is
well understood:

- `pg.copy(conn, table, rows)` over `COPY ... FROM STDIN`;
- `odbc.insert(conn, table, rows)` binding a batch as arrays.

`gsql.insert` already shows the shape: many rows, all or nothing, refusals by
row.

**G3. No columnar files.** Data lakes and warehouses exchange **Parquet**
(Snowflake, BigQuery, Databricks and pandas unloads, S3 extracts). gBASIC
cannot read it. Reading is the need; writing comes second. A reader needs:

- Parquet's metadata (Thrift's compact encoding);
- its page encodings;
- its compressions: Snappy, gzip and zstd. Only gzip exists here.

That is a module the size of `xlsx`'s reader. Arrow and Avro are rarer in
files handed between companies.

**G4. No object storage.** S3 and its compatibles (MinIO, Azure Blob's S3
gateway, Cloudflare R2) are where extracts land. AWS's SigV4 request signing
is HMAC-SHA256 over a canonical request, and `hmac_sha256` and `http` already
exist. So this is a stdlib library (list, get, put, multipart), not a C
module.

**G5. Reads that use one core.** This is the parallel-readers proposal
(`docs/gsql_parallel_readers_design.md`), already on the list. Its option D,
a read-only replica refreshed by `gsql.backup`, is the enterprise pattern of
"reports run on a copy".

**G6. Work across G-SQL databases.** `docs/gsql_multidb_design.md` says
what already combines (count, sum, min and max, and averages done right) and
lists primitives, none built. Of those, the **change feed** (§2.3) is the one
an enterprise asks for: "send downstream what changed since last time" is how
data moves between systems. Commit ids and two-phase commit can wait until a
program needs them.

**G7. No message queues.** There is no Kafka, AMQP (RabbitMQ) or MQTT. Some
shops move all their data that way; many move none. **Whether to do any of
them depends on where gBASIC will be used** (§5 Q5).

**G8. No job runner.** `timer` and `intake` exist. There is nothing that
records each run of a nightly job (started, finished, failed, retried,
rows), which is what an operator asks for first. A small library over G-SQL
or `persist` would do it. It is not urgent.

## 3. The PostgreSQL wire protocol, priced

**The protocol itself is the small part.** Version 3 is framed messages:

- startup and authentication (SCRAM-SHA-256; `crypto` has the pieces);
- the simple query flow;
- the extended flow: parse, bind, execute.

Results in text format first. A front end in the owner, or beside it,
translates these into the requests G-SQL already serves.

**The large part is what clients send on connecting.** `psql`, the JDBC and
npgsql drivers, and every BI tool query `pg_catalog` and
`information_schema` to learn tables, columns and types. Some send `SET` and
`SHOW` commands, and a few send Postgres-only SQL. To be useful, G-SQL would
answer those catalog views from its own catalog, at least the subset the
common clients use. This is measurable, not a guess: capture what `psql`,
psycopg, npgsql and Metabase send at connect, and implement exactly that.

**The risk is a long tail.** Each new client finds a catalog query not yet
answered. The mitigation is the same as everywhere here: a corpus of real
clients' connect traffic, and a test that every query in it is answered.

**The recommendation: yes, after parallel readers, as its own design note,
starting with the connect-traffic capture.** It is G1's only route with a
small surface, and every later integration rides on it. Encryption is
`net`'s TLS. Authentication is a G-SQL question first: G-SQL trusts the local
socket today, and a network listener needs users and passwords.

## 4. The discovery test estate

`docs/estate_design.md` holds **requirements only**, by Matthew's decision
(2026-09): if I design the traps, I plant only the ones I would think to
detect. The suggestion was that gdash, or another outside party, supplies
them.

`discovery` and `nlq` are built against what exists:

- declared facts verified against four databases;
- a committed catalog;
- `stdlib/estate.bas`.

**The test estate is what would show whether they find lineage nobody
declared.**

**This stays parked until an outside author exists.** The alternative, a
fully mine estate, is the self-generated-evidence problem again: the finance
libraries' fixtures are all mine, which is why real loan data is on the list.
If gdash
can take it on, the requirements are ready for them.

## 5. Recommendations and questions for Matthew

In order of value for the effort:

1. **G2, bulk load** (`pg.copy`, ODBC array inserts): small, and met by every
   real job. **Recommended for 0.13.0.**
2. **G5, parallel readers**: already planned, awaiting its Q1-Q5.
3. **G1, PostgreSQL's wire protocol**: a design note after parallel readers,
   starting from captured client traffic.
4. **G3, Parquet reading**, and **G4, S3**: when a job needs them; G4 is
   cheap.
5. **G6, the change feed**: when a second system needs to follow a G-SQL
   database.

Questions:

- **Q1.** Agree with this order?
- **Q2.** The wire protocol (§3): yes, as a design note after parallel
  readers?
- **Q3.** Bulk load (G2) in 0.13.0?
- **Q4.** The discovery estate (§4): leave it parked for an outside author,
  or ask gdash now?
- **Q5.** Message queues (G7): do the places gBASIC is meant for move data
  through Kafka, RabbitMQ or MQTT? If none, they stay off the list.
