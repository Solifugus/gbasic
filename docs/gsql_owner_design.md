# G-SQL's owner and protocol (Phase 1, increment 2)

**Status:** designed and built 2026-10-29 (`include/gsql_wire.h`, `include/gsql_owner.h`, `include/gsql_client.h`, `src/gsql/wire.c`, `src/gsql/owner.c`, `src/gsql/client.c`, `tests/run_gsql_owner.sh`). It is the second layer of Phase 1 (`gsql_table_design.md` §1). It turns the prototype in `research/gsql/roundtrip.c` into the owner that `gsql_design.md` §6.1 and §6.5 describe. Where this page and those sections differ, this page records what was built and why.

## 1. What this increment is, and what it is not

**It is** the transport, the owner process and the client side in C:

- the frames and their bodies (§2);
- where the socket lives and who becomes the owner (§3);
- the owner's single-threaded loop, its connections and their transactions (§4, §5);
- the client: connect, or start an owner and wait for it (§6);
- the handoff, when an owner disappears (§7).

**It is not the SQL.** `QUERY`, `PREPARE`, `FETCH` and `CLOSE` carry SQL, and there is no SQL layer yet; that is increment 3. So the owner hands those messages to a **service**, an interface it is given (§5.3). It manages everything around the service itself: transactions, the one writer, cursor ids, batches, timeouts, a dropped connection. The owner the product will run is a `gbasic` process whose service is the SQL layer. The tests run an owner whose service is a small one written in the test. That service exercises every path the owner owns and is never shipped.

**It is not linked into `gbasic` yet**, and that is deliberate. An owner whose every query answers "there is no SQL layer" would add the socket, poll and process code to every build, Windows and lean included, for nothing a program could use. Increment 3 links it, when the owner first has something to answer. Every line here is written to be linked: no globals beyond one signal flag, and every allocation freed.

## 2. The wire

### 2.1 Frames

Exactly §6.5's frame:

| bytes | field |
|---|---|
| 4 | length of the rest, little-endian |
| 1 | message type |
| 4 | request id, chosen by the client, echoed in the reply |
| rest | the body: ONE `serialize` v3 value |

A frame is at most 16 MiB. **A length under 5 is `PROTOCOL` and a length over the limit is `TOO_LARGE`.** Either way the stream cannot be resynchronised, so the owner answers the error and closes the connection.

**Frames are reassembled from whatever the socket delivers** (`gsql_wire_feed`). The prototype read each frame with a blocking `read` loop, so **a client that sent half a frame stalled the owner and every other client with it**. The owner reads what is there, keeps the partial frame with its connection, and goes back to `poll`. The output side works the same way: replies are queued per connection and written as the socket accepts them. A client that never reads its replies therefore cannot block the owner. Once its queue passes a bound, the owner stops reading its requests until the queue drains.

### 2.2 Bodies are records

Every body is a v3 **record**, not the arrays the first sketch had. A newer minor version may add a field, and an older reader ignores a field it does not know. A record does this by construction; an array does it only if every array happens to grow at the end. The bodies use the table layer's reader and writer (`src/gsql/v3.c`), so the protocol and the rows agree on what a well-formed value is.

| type | body |
|---|---|
| `HELLO` | `major`, `minor`, `client` (text), `pid` |
| `WELCOME` | `major`, `minor`, `pid`, `device`, `inode`, `format` |
| `REFUSED` | `code`, `message` |
| `QUERY` | `statement` (text, or a prepared id), `parameters` (array), `batch` (rows per batch), `principal` |
| `ROWS` | `cursor` (absent when nothing remains), `more`, `columns` (first batch only), `rows` |
| `FETCH` | `cursor`, `count` |
| `CLOSE` | `cursor` |
| `PREPARE` | `statement` |
| `PREPARED` | the service's description |
| `BEGIN` | `mode`: `"read"` or `"write"` |
| `COMMIT`, `ROLLBACK`, `PING`, `BYE` | empty record |
| `CANCEL` | `request` |
| `DONE` | `rows` (affected, if any), `commit` (the commit id, if it wrote), `cancelled` (for a `CANCEL`) |
| `PONG` | empty record |
| `ERROR` | `code` (text), `message`, `position` (where the SQL has one) |

**Error codes are text** (`"DUPLICATE"`, `"TOO_LARGE"`), never numbers. A reader of a newer owner then still prints what happened. The closed set:

- the table layer's: `NOT_FOUND`, `EXISTS`, `DUPLICATE`, `NOT_NULL`, `TYPE`, `SCHEMA`, `CORRUPT`, `STORAGE`, `MISUSE`;
- the protocol's: `PROTOCOL` (a malformed frame or body), `VERSION`, `TOO_LARGE`, `CANCELLED`, `UNSUPPORTED` (no service, or a message the service does not implement), `STATE` (`COMMIT` with nothing begun, `BEGIN` inside a transaction), `READ_ONLY` (a write statement in a `BEGIN READ`), `FAILED` (the transaction failed earlier and must be rolled back), `TIMEOUT`;
- SQL's own: parse, binding and constraint codes, added by increment 3.

## 3. Where the socket lives, and who owns the database

**The socket is named from the database's identity on disk**, its device and inode (`gsql_design.md` §6.5), so two paths reaching one database through a symlink find one owner. A G-SQL database is a directory (`gs_db_dir`), so the identity is the directory's.

**The runtime directory** is `$XDG_RUNTIME_DIR/gbasic`. Where `XDG_RUNTIME_DIR` is unset it is `/tmp/gbasic-<uid>`. The owner creates it at mode 0700 and then **checks it rather than trusting it**. It must be a directory, not a symlink, owned by this user, and accessible to no one else. §6.5's trust model rests on this check: an owner trusts every client because only this user can reach the socket. A directory someone else made in `/tmp` is the classic attack on that, and it is refused by name. The socket path must fit `sun_path` (108 bytes), or it is refused by name.

**The lock decides.** The engine takes the database's exclusive lock on open (`lsm.c`, a `.lock` file under `flock`). An owner that gets it is the owner. It binds the socket under a temporary name and **renames** it over the real one, so a stale socket left by a crashed owner is replaced in one step, and a client never finds the name missing between an unlink and a bind. An owner that loses the lock (`GS_BUSY`) has lost a race. It reports that and exits.

**On a clean exit the owner removes the socket before closing the store**, which releases the lock. The socket is never removed by a process that does not hold the lock.

## 4. The owner's loop

The owner is single-threaded (§6.1): one `poll` over the listening socket and every connection, with non-blocking sockets. Each connection holds:

- its state: waiting for `HELLO`, then ready;
- an input reassembly buffer and an output queue;
- a queue of decoded requests not yet answered;
- its transaction: none, read, write, or failed;
- its cursors.

**Requests are answered in order.** A connection's requests are decoded into its queue and answered from the front. A request that cannot proceed yet (a `BEGIN WRITE` while another connection writes) **parks** its connection, and the requests behind it wait. Other connections carry on.

**One writer.** The engine allows one write transaction, and the owner gives the writer slot to one connection at a time. A connection that wants it while another holds it joins a FIFO and is resumed when the holder commits, rolls back, times out or disconnects. A write statement in autocommit mode queues the same way. A read never waits: a snapshot can be taken while another connection writes.

**`CANCEL` is the one request read out of order.** A single-threaded owner has only one kind of pending work, a parked request. So `CANCEL` names a request id; if that request is parked, it is answered `ERROR CANCELLED` at once, and it is the front of the queue, so order holds. `CANCEL`'s own reply, `DONE` with `cancelled` true or false, takes its place in the queue like any request. Cancelling a long batch between `FETCH`es is `CLOSE`. **Cancelling work already running is not possible in this owner, and the protocol does not pretend it is** (§6.5).

**Signals.** On `SIGTERM` the owner stops accepting work and rolls back any open write transaction. It then calls the engine's `flush`, the planned handoff that leaves the next owner nothing to replay, removes its socket, closes the store and exits 0. **The request carries a deadline** (`stop_grace_s`, 10 seconds by default): `SIGTERM` also arms `alarm`, and `SIGALRM`'s default action ends the process if the clean stop has not finished. An owner stuck in a loop of its own never returns to the poll that sees the request, and without the deadline it would ignore its client's death and hold the database's lock for ever. One was found doing exactly that while this increment was tested: an orphan spinning at full CPU with no system calls. `SIGPIPE` is ignored; a vanished client is a failed write, handled like a disconnect.

## 5. Transactions and the service

### 5.1 Explicit transactions

- **`BEGIN READ`** takes a snapshot, held by the connection until `COMMIT` or `ROLLBACK`. Every statement in it reads that snapshot. A statement the service says writes is refused `READ_ONLY`.
- **`BEGIN WRITE`** takes the writer slot, waiting for it if necessary, and a write transaction. `COMMIT` commits it, durably, and answers its commit id. `ROLLBACK` aborts it.
- A `BEGIN` inside a transaction, or a `COMMIT` or `ROLLBACK` with none, is `STATE`.
- **A refused statement leaves the transaction usable** (the table layer's promise). An engine failure that ends the transaction, `GS_FULL` or `GS_IO`, marks it **failed**. Every later statement is `FAILED` until `ROLLBACK`, which is how a SQL session reports that its transaction is gone, not something that quietly carries on outside it.

### 5.2 Autocommit

A statement outside a transaction runs in its own. The service says whether a statement reads or writes, which only the SQL layer can know. A read gets a snapshot. A write gets the writer slot, waiting if necessary, then a write transaction that is committed if the statement succeeds and aborted if it fails. **A read that produces more rows than one batch keeps its snapshot in its cursor** until the rows are exhausted or the cursor is closed. A read without a cursor releases the snapshot at once.

### 5.3 The service

```c
typedef struct {
    void *ctx;
    int (*writes)(void *ctx, const GsqlStatement *s);
    int (*run)(void *ctx, GsTxn *t, const GsqlStatement *s, GsqlResult *out, GsqlError *err);
    int (*fetch)(void *ctx, void *cursor, uint32_t count, GsqlResult *out, GsqlError *err);
    void (*close)(void *ctx, void *cursor);
    int (*prepare)(void *ctx, const GsqlStatement *s, GsqlBuf *description, GsqlError *err);
} GsqlService;
```

- `run` executes a statement in the owner's transaction. It answers either rows affected, or a first batch of rows with an optional cursor the owner keeps.
- `fetch` continues a cursor; `close` frees it.
- The owner numbers cursors. It closes a connection's cursors when the transaction they were opened in ends (a cursor over a write transaction is meaningless after it), when the connection drops, and when a cursor is idle past the timeout. The next `FETCH` on a timed-out cursor answers `TIMEOUT`, saying why (§6.5).
- `prepare` may be `NULL`, and the owner then answers `UNSUPPORTED` by name. With no service at all, every statement answers `UNSUPPORTED`, saying that the SQL layer is not in this build.

### 5.4 Timeouts

Both are owner options, 300 seconds by default, as §6.5 proposed for cursors:

- **An idle cursor** is closed, because its snapshot keeps the engine from deleting files a compaction has replaced.
- **An idle write transaction** is rolled back, and the slot passes to the next connection. Its next request answers `TIMEOUT`, and so does every request after that until `ROLLBACK`. §6.5 named only the cursor timeout. An abandoned writer is the same hazard in a worse form: it stops every other writer, not just one file's deletion.

**A dropped connection** rolls back its write transaction, releases its snapshots and cursors, and leaves the writer FIFO (§6.5).

## 6. The client

`gsql_client_open(path, options)`:

1. Resolve the database's identity, then try to connect. If a connection succeeds, handshake and return.
2. Otherwise start an owner: `fork`, then (on Linux) `PR_SET_PDEATHSIG` with `SIGTERM`, checked against `getppid` for a parent that died first, then `exec` of the owner program the options name. The owner is the client's child and dies with it (§6.1), and `SIGTERM` gives it the planned handoff of §4.
3. **Readiness is reported, not polled for.** The client passes a pipe, and the owner writes one line on it:
   - `ready`, once the socket is bound;
   - `busy`, if it lost the lock;
   - `error <message>`, if the open failed: a newer format, corruption, a refused runtime directory.

   The prototype polled `connect` in a loop for up to five seconds, and an owner that failed to open looked exactly like a slow one. Now the client hears why.
4. On `ready` or `busy`, connect. A `busy` owner lost to another owner that may not have bound its socket yet, so the client retries the connection for a bounded time. If that time passes, it starts again from step 1, since the winner may itself have died. The attempts are bounded, and running out of them is an error that says how many there were.

A database that does not exist is created only if the options ask (`create`), and then **by the owner**: the client cannot take an identity from a directory that is not there yet. Two clients creating at once race to the lock like any two owners.

**The handshake** is `HELLO` / `WELCOME`. A different protocol major is `REFUSED` with code `VERSION`, naming both versions. A newer minor is accepted. A first frame that is not `HELLO` is `REFUSED` `PROTOCOL`.

## 7. The handoff

When an owner disappears, each client's connection ends. Whatever was in flight fails with a message saying that the owner exited and the transaction was rolled back. **Nothing is retried silently** (§6.5). The connection is marked lost, and the client's next request goes back through §6, which reconnects or starts an owner. That owner opens the store, so recovery runs on every handoff.

## 8. How it is tested

`tests/run_gsql_owner.sh`. The tiers:

- **build**, under `-Wpedantic -Werror`.
- **wire:** frames split at every byte, coalesced, at the limit and past it.
- **protocol:** the handshake and its refusals. Each transaction rule beside its control, pipelining, `CANCEL` of a parked request, and both timeouts. A dropped connection's rollback, observed by another connection.
- **stall:** a client that sends half a frame, and a client that never reads, while another client is served. This is the prototype's defect.
- **race:** several clients start at once on a database with no owner, and exactly one owner results.
- **paths:** a symlink to the database finds the same owner. A stale socket is replaced. A runtime directory someone else could reach is refused.
- **handoff:** the owner is killed with `kill -9`. The in-flight transaction is reported failed, committed work survives, and the next request starts a new owner.
- **lifetime:** the client is killed with `kill -9`, and the owner it started goes too.
- **cost:** the round trip, against §6.1's gate of about 20 µs added to a point read. **Measured 2026-10-29: about 5 µs added at the median** (5.45 µs through the owner, 0.49 µs in process).

Twenty-three perturbations are proven red, each by the tier written for it. Six of them first passed, or failed for the wrong reason, until the suite itself was fixed. That suite history is in `CLAUDE.md`'s entry for `run_gsql_owner.sh`.
