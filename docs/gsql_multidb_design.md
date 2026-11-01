# G-SQL across many databases: primitives for gBASIC to build on

**Status:** Proposal, not scheduled (2026-11-01). Matthew, the same day: aggregates and transactions across databases are "too much at any time soon", but worth designing for. The approach is for G-SQL to supply primitives and for gBASIC to do the coordinating, not for G-SQL to grow a distributed engine. Nothing below is built, unless §1 says it already works.

## 0. Why it is gBASIC's

**Scaling out means many databases, each with one owner** (`gsql_owner_design.md`), coordinated by a gBASIC program. How data is split is the application engineer's decision, not the engine's:

- one database per tenant, as gdash and WorkSplicer will do;
- or one large table split by key or date.

gBASIC already has what coordination needs:

- shared-nothing actors and worker pools;
- non-blocking `http`;
- owners started on demand;
- databases cheap enough to have thousands of: about 1 MB empty (`gsql_storage_design.md`).

Keeping the routing rule in the application's own code is also what an enterprise usually wants.

## 1. What already works, needing nothing new

**The common aggregates combine across databases.**

- `count`, `sum`, `min` and `max` from each database merge directly.
- An average is `sum(x)` and `count(x)` from each, added, then divided once. Averaging the averages is wrong whenever the databases hold different numbers of rows.
- A grouped report is the same per group: each database's groups, merged by key in gBASIC.

**Idempotent writes.** A primary key or `UNIQUE` column holding a request or event id makes a retried write a refused duplicate, never a second copy. That is the building block for:

- an **outbox**: the write and its outgoing message committed in one transaction in one database, the message delivered and marked done later;
- **at-least-once delivery** made safe on the receiving side.

**No transaction spans two databases.** A program that commits to two databases has two independent commits. A crash between them leaves one done and one not. The patterns above are how an application stays correct with that.

## 2. Primitives G-SQL could add, cheapest first

### 2.1 Commit ids handed to the program

Every commit already has a number in its database (the engine's `txn_id`). The version store reads `AS OF` one (`gsql_storage_design.md` §4).

- `gsql.commit` and autocommit `exec` would answer the id.
- A read could be asked for `AS OF` an id.

**What it enables:**

- a program recording how far it has seen in each database;
- a "read your own writes" guarantee across a pool of readers;
- a report taken at a stated point in each database.

**Cost:** small. The id exists; this is a reply field and a read option.

### 2.2 Mergeable state for the aggregates that do not add up

Median, percentiles, `count(DISTINCT ...)` and variance cannot be combined from finished answers. A query would instead return each aggregate's **unfinished state**, and gBASIC would merge the states and finish them. This uses the `init → accumulate → merge → finalize` shape `gsql_design.md` §7 already plans for statistics, so the kernels serve both.

**Open questions:**

- the state's representation, which must be a portable value that survives `serialize`;
- exact against approximate state: an exact median needs every value;
- the rule for money, whose variance is in squared units (`gsql_design.md` §3).

### 2.3 A change feed

Each database's committed changes, in commit order, from a given commit id, as rows a program reads. The log-structured engine already writes changes in that order.

**What it enables:**

- an outbox reader that needs no polling query;
- read replicas;
- summaries in one database kept current from changes in others;
- an audit trail outside the database.

**Open questions:**

- **Retention:** how far back a feed can start, since compaction discards old versions.
- **Consumers that fall behind:** whether the database keeps history for a slow consumer, or the consumer resynchronises from a snapshot.
- **Schema changes:** how a feed reports one.

### 2.4 Prepared transactions (two-phase commit)

**The mechanism:**

- `PREPARE TRANSACTION name` makes a transaction durable but **undecided**. It survives a crash in that state.
- `COMMIT PREPARED name` and `ROLLBACK PREPARED name` settle it, possibly from a later connection or after a restart.
- A gBASIC coordinator would prepare in every database, then commit in every one. It would record its decision durably first, so that it can finish after its own crash.

**Why it is the costly one.** Each database has one writer, so a prepared and undecided transaction stops that database's writes until it is settled. It needs at least:

- a timeout;
- a way to list stranded prepared transactions after a crash;
- a rule for who may settle them.

Nothing about it should be built before a real application needs atomicity across databases that the §1 patterns cannot give it.

## 3. What G-SQL should not do

- **Choose a partitioning.** That is the application's.
- **Plan queries across databases.** A coordinator that hides where data lives also hides what a query costs. A fan-out written in gBASIC states its cost.
- **Coordinate distributed transactions itself.** Supplying §2.4's primitive is enough; the coordinator is application code.
