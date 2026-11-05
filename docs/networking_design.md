# Networking: sockets, TLS, datagrams and WebSockets

Status: **Ruled 2026-11-04** (§13: Matthew took every recommendation); **N1 shipped 2026-11-04** (`load net`, `pack`/`unpack`;
`tests/run_net.sh`); **N2 shipped 2026-11-04** (TLS: `tls:` on connect and
listen, `start_tls`, §5 and the note below it); **N3 shipped 2026-11-04**
(UDP: `net.datagram`, §4 and the note below it); **N4 shipped 2026-11-04**
(`load websocket`, §6 and the note below it). N5 is designed when a server
needs it. Asked for by
Matthew, to fill the gap beneath two named uses and the ones that will follow:

1. **An SSH server and client for Transward**, which today drives OpenSSH
   underneath (and had to generate a shell wrapper per run just to pass
   `SSH_ASKPASS`, `docs/reference.md`, process `env`).
2. **A networked RDBMS with G-SQL underneath**: G-SQL today is reached only by
   gBASIC programs on the same machine, over a Unix socket to its owner process.

Neither is in this document. This document is the layer both stand on. §9 and
§10 list what each needs *beyond* networking, so that list is not discovered
halfway through either project.

## 1. What exists, and the gap

gBASIC can speak HTTP in both directions and three other protocols, all through
libraries that own the socket:

| exists | what it is |
|---|---|
| `webclient` | one HTTP/HTTPS request, blocking (libcurl) |
| `http` | the same request non-blocking, readiness delivered to `watch(http.events)` |
| `webserver`, `server` block | HTTP/1.1 server: TLS with SNI, worker pools, SSE streaming, hardening |
| `smtp` | sending mail (libcurl) |
| `ldap` | bind and search (libldap) |
| `mcp` | the Model Context Protocol over stdio and HTTP |
| G-SQL owner | its own framed protocol over a **Unix** socket, internal |

**A program cannot open a socket.** There is no TCP client or server, no Unix
socket a program can name, no UDP, no WebSocket in either direction (the
webserver refuses an upgrade by design), no DNS lookup a program can call, and
TLS exists only on the server side of `webserver`. Every protocol that is not
HTTP has to be a C module or a child process.

## 2. Principles, and the measurement or precedent behind each

**P1. ONE LOOP.** gBASIC's event loop is one `poll()` over the webserver's
descriptors, libcurl's, and the root mailbox, with timers riding the timeout
(`webserver_loop_iterate`). With a window open, Qt owns the wait and a Qt timer
drives the **same function** (qt_gui_design.md §5.1, measured: the other
arrangement starves every source while a modal dialog is up). So a socket is
one more descriptor in that function and nothing else. It is serviced the same
with a window up, a dialog open, or neither. No second loop, no threads
running gBASIC code.

**P2. WAITED OR WATCHED, NEVER BOTH**, as `http` handles are. A script that
connects, sends and reads can block, with a timeout. A server that holds a
thousand connections watches `net.events` and never blocks. A connection is one
or the other. A blocking read inside a watcher stalls every other source, so it
warns, exactly as `http.wait` inside a watcher does today.

**P3. BYTES, COUNTED.** A gBASIC string is counted bytes and NUL is content
(PLAT-NUL). Every length and position in this module is **bytes**, never
codepoints, and that is a trap to state loudly: `len(s)` counts codepoints, so
framing code must use `byte_count` (the trap `run_read_at.sh` already records
for files). Nothing here decodes text unless asked.

**P4. FRAMING IS THE PROGRAM'S, BUT THE BUFFER IS THE MODULE'S.** Each
connection has a receive buffer, and the module offers the three cuts every
protocol needs (exactly `n` bytes, up to a delimiter, a line), so SSH's
length-prefixed packets, PostgreSQL's typed messages and SMTP's lines are each a
few lines of gBASIC rather than a reassembly loop with an off-by-one in it.
`http.read` frames nothing and says so, and that was right for a body. For a
protocol it is the bug factory.

**P5. BOUNDED BY DEFAULT.** A buffer has a ceiling, a connection has an idle
timeout, a listener has a connection cap, and a connect has a timeout. Each is
an option with a stated default, and exceeding one is a named event, never
silent growth. A server that reads faster than the program consumes **stops
reading** (POLLIN off) at the high-water mark rather than buffering a
client's whole upload in memory.

**P6. LOOPBACK BY DEFAULT.** `net.listen` binds 127.0.0.1 unless an `address`
is given, the rule `webserver.listen` already has (`run_web_bind.sh`). A
database or SSH server reachable from the network is a decision a program
writes down.

**P7. FAILURES ARE NAMED, NOT CLASSIFIED.** "Refused", "timed out", "reset by
peer", "no such host", "unreachable" and "certificate did not verify" want
different fixes, as `ldap`'s distinct answers do. Each refusal raises with a
stable `details.code` (§7), and an event carries the same code.

**P8. CORE, NOT OPTIONAL. TLS IS OPTIONAL.** Sockets are POSIX, need no
library, and so build everywhere, like `process`. TLS needs libssl and sits
behind `HAVE_LIBSSL`. Without it, `tls:` is refused with a sentence, as every
compiled-out feature is.

## 3. The surface: `load net`

`load net` binds a global record `net` holding `events`, as `load http` does.

### Connecting (client)

```basic
load net

c = net.connect({ host: "db.example.com", port: 5432, timeout: 10 seconds })
c.write(startup_message)
reply = c.read_exactly(5, 30 seconds)   ' waited: blocks, with a timeout
c.close()
```

- `net.connect(options) -> connection`. Options: `host` and `port`, or `path`
  for a Unix socket; `timeout` (connect timeout, default 30 seconds);
  `tls` (§5); `idle` (default none on a client); `buffer` (receive ceiling,
  default 16 MiB).
- **A connection is waited until it is watched.** If a watcher on `net.events`
  is live when it is made, it is watched (as `http` decides at start).

### Listening (server)

```basic
load net

watch(net.events)
    while count(net.events) > 0
        e = take_first(net.events)
        consider e.kind
            if "accepted" then
                e.connection.write("hello" + chr(10))
            if "data" then
                line = e.connection.take_line()
                while not is_unknown(line)
                    e.connection.write("you said " + line + chr(10))
                    line = e.connection.take_line()
                end while
            if "closed" then
                print "gone: " + string(e.connection.peer)
        end consider
    end while
end watch

srv = net.listen({ port: 7000 })        ' 127.0.0.1 unless `address:` says otherwise
```

- `net.listen(options) -> listener`. Options: `port` (0 asks the OS, read it
  back from `listener.port`), or `path` for a Unix socket (and `mode`, its
  permissions); `address` (default `127.0.0.1`; `"0.0.0.0"` and `"::"` are
  explicit); `backlog`; `max_connections` (default 1024); `idle` (default
  5 minutes for an accepted connection); `buffer`; `tls` (§5).
- **Watched:** each new connection arrives as an `accepted` event. **Waited:**
  `listener.accept([timeout]) -> connection` blocks for one.
- `listener.close()` stops accepting. Open connections are untouched.

### Events

Each event is `{ kind, id, connection, listener, code, message, bytes }`:

| kind | when |
|---|---|
| `accepted` | a listener took a connection (`listener` set) |
| `connected` | a watched connect finished (`connect` returns at once when watched) |
| `data` | bytes arrived; reported once per arrival, and again only after a `take` |
| `writable` | the send queue fell below its low-water mark after being above it |
| `closed` | the peer closed, or the program did; `code` says which |
| `error` | the connection failed (`code`, `message`); a `closed` follows |
| `full` | the receive buffer reached its ceiling; reading has paused |
| `idle` | the idle timeout passed; the connection is closed after the event |

As with `http`, **the loop holds a reference to every watched connection**, so
`c` being rebound per request does not close the previous one. A watched
connection lives until it is closed, by either side or by its idle timeout.

### Reading: three cuts, two families

The **take** family never blocks and answers `unknown` when what was asked for
has not fully arrived. It is the one to use in a watcher:

- `c.take([max])`: whatever is buffered (up to `max` bytes), possibly `""`.
- `c.take_exactly(n)`: exactly `n` bytes, or `unknown`.
- `c.take_until(delimiter [, { include: false }])`: up to the delimiter, or `unknown`.
- `c.take_line()`: up to `\n`, with a trailing `\r` removed, or `unknown`.
- `c.peek_exactly(n)`: the same as `take_exactly` but leaves the bytes buffered.
  This is what reading a length prefix needs, since the length alone is not a
  frame.
- `c.buffered()`: how many bytes are waiting.

The **read** family blocks, with a timeout, and is for waited connections:
`c.read([max], [timeout])`, `c.read_exactly(n, [timeout])`,
`c.read_until(delimiter, [timeout])`, `c.read_line([timeout])`. A timeout
raises with code `timeout` and leaves what arrived buffered. End of stream before
the cut is complete raises with code `closed` and the partial bytes in
`details.partial`. A frame cut short by a close is a different fact from a
frame not yet arrived, and the take family's `unknown` cannot say which.

**A delimiter or length past the buffer ceiling is refused at the call**, so a
`take_until` that can never be satisfied says so at once, rather than waiting
for the `full` event.

### Writing

- `c.write(bytes) -> queued`: appends to the send queue and answers the byte
  count queued. It never blocks a watched connection. The loop drains the queue
  as the socket accepts it, so a partial `send` is the module's problem, not the
  program's.
- `c.queued()`: bytes not yet sent. Past the send ceiling (default 16 MiB), a
  write is refused with code `backpressure`. The `writable` event is the signal
  to continue.
- `c.flush([timeout])` (waited): blocks until the queue is empty.
- `c.shutdown_write()`: half-close, the end-of-request signal some protocols
  use. `c.close()` closes after the queue drains. `c.abort()` closes now and
  discards it.

### What a connection says about itself

`c.id`, `c.peer` and `c.local` (`{ address, port }`, or `{ path }`),
`c.open`, `c.watched`, `c.tls` (`{ version, cipher, peer_certificate }` or
`nothing`), and for a Unix socket `c.peer_credentials` (`{ pid, uid, gid }`,
from `SO_PEERCRED`). The last is what lets a local database authenticate a
user by who they are, as PostgreSQL's `peer` authentication does.

### Names

- `net.resolve(host) -> [ { address, family } ]`. A name lookup is
  `getaddrinfo`, which blocks. In a waited connection that is acceptable. In
  the loop it is not, so a **watched** `net.connect` to a name resolves off the
  loop: a helper thread that runs only `getaddrinfo` and writes to a pipe the
  loop polls. No gBASIC code runs on it. That is how libcurl avoids the same
  stall, and the reason `http` never blocked on a name.
- `net.connect` tries each resolved address in order (IPv6 and IPv4) until one
  connects, within the one timeout.

## 4. Datagrams (UDP)

```basic
u = net.datagram({ port: 5140 })               ' bound; loopback unless `address:`
u.send_to("<13>hello", { host: "127.0.0.1", port: 514 })
watch(net.events)                              ' kind "datagram": e.data, e.from
```

`net.datagram(options) -> socket`. `send_to(bytes, to)`, and in waited mode
`receive([timeout]) -> { data, from }`. Watched, each datagram is an event of
kind `datagram`. **A datagram is never split or joined**: one event is one
datagram. One larger than `max_datagram` (default 64 KiB) is reported as
truncated (`truncated: true`), never silently cut. Multicast and broadcast are
options (`broadcast: true`, `join: "239.1.2.3"`) for a later increment.

**As built (N3).** As drafted, plus three decisions the draft did not make:

- **A socket that names no port is a client's** and binds to `0.0.0.0` on a
  port the system picks, which is what an unbound UDP socket gets at its first
  send. One that names a port is a server's and binds to loopback (P6). A
  client bound to loopback could reach nothing but loopback, and making every
  client say `address: "0.0.0.0"` is one more thing to remember for no safety:
  an ephemeral port serves nothing until something is sent from it.
- **No send queue.** `send_to` hands the datagram to the system before it
  returns, waiting for room within a timeout (default 10 seconds) when there is
  none. A queue would add `writable`, `queued` and drain-on-close to datagrams
  for a case loopback cannot produce: the kernel drops a datagram a full
  receiver has no room for rather than refusing the send, so the wait is
  reachable only on a link slower than the program, and is NOT exercised by
  the suite.
- **The target is a record, and a received `from` is one**, so a reply is
  `u.send_to(answer, e.from)`. `address:` is numeric; a name goes in `host:`
  and is resolved, blocking, as `net.resolve` is.

Datagram events name the socket as `socket` (it is neither a `connection` nor
a `listener`). `size` is the length that was sent, which Linux reports
(`MSG_TRUNC`); elsewhere it is `unknown` for a truncated datagram. Unix
datagram sockets, multicast and broadcast are not built.

## 5. TLS

Over libssl, which `webserver` already links for its own TLS.

- **Client:** `net.connect({ host:, port:, tls: true })`, or
  `tls: { server_name:, ca:, verify: true, certificate:, key: }`.
  **Verification is ON**: the system trust store, and the host name checked
  against the certificate (`X509_VERIFY_PARAM_set1_host`). `verify: false` is
  spelled out by whoever wants it. A failure raises code `tls_verify`, naming
  what failed (expired, wrong name, unknown issuer). `certificate` and `key`
  give a client certificate (mutual TLS).
- **Server:** `net.listen({ port:, tls: { certificate:, key:, sni: {...},
  client_ca:, require_client_certificate: } })`. This is the same certificate
  loading and SNI selection `webserver` has, shared rather than copied.
- **`c.start_tls(tls_options)`: upgrade a connection that is already open.**
  This is load-bearing rather than a nicety. PostgreSQL negotiates TLS this way
  (an `SSLRequest` message in plaintext, one byte back, then the handshake on
  the same socket), as do SMTP's `STARTTLS`, LDAP's and IMAP's. A TLS-only
  `connect` could not speak any of them. The handshake runs in the loop. In a
  watched connection, `connected` (for a client) or a `tls` event (for an
  upgrade) says it finished. In a waited one the call blocks, with a timeout.
- **The peer's certificate is readable**: `c.tls.peer_certificate` is
  `{ subject, issuer, not_before, not_after, sha256 }`, so a program can pin a
  certificate by fingerprint.

**As built (N2), and where it differs from the draft above.** The server
options are spelled as `webserver.listen`'s are (`cert`, `key`, and `certs:
[{ host, cert, key }]` for SNI) rather than the draft's `certificate:` and
`sni: {...}`: the loading is shared code (`webserver_tls_build`), and one
vocabulary for one thing is less to remember. Mutual TLS is `client_ca` and
`require_client_cert`, and a client certificate is `cert` and `key` on the
client's record. Three decisions the draft did not make:

- **`start_tls` refuses bytes already buffered** (`unexpected_data`). They
  arrived in plaintext; reading them as part of the TLS stream is the STARTTLS
  injection (CVE-2011-0411 and its family).
- **A TLS listener announces a connection after its handshake**, so `c.tls` is
  ready in the `accepted` handler. A handshake that fails is an `error` event
  naming the connection and its listener, with no `accepted`.
- **A refused client certificate is named the same however it lands.** TLS
  1.3 refuses it after the client's handshake has finished, so a client finds
  out at its next read or write. When that write meets the closed socket first,
  the alert waiting behind it is read, so the failure is `tls_failed` naming
  the alert either way, not a broken pipe one time in some.

## 6. WebSockets: a library over `net`, not a module

RFC 6455 framing (opcode, masking, length forms, ping/pong, close codes) is a
few hundred lines of gBASIC over the take family. The handshake needs `sha1` and
`base64_encode`, which exist. Written in gBASIC it is readable, testable without
a C compiler, and the `net` module stays small.

- **Client:** `websocket.connect("wss://host/path" [, options])` over
  `net.connect` with `tls`. Events arrive as `message` (text or binary),
  `ping`/`pong`, `closed` with the close code.
- **Server:** the webserver hands a request that asks to upgrade over to `net`:
  an upgrade verb on `web` (to be named in N4) writes the 101 response, removes the client
  from the HTTP side, and the socket becomes an ordinary watched `net`
  connection. `websocket.accept(connection)` then frames it. This is the one
  change inside `webserver`: today an upgrade is refused, by design, because
  nothing could take the socket afterwards.
- **In a worker pool**, an upgraded connection belongs to the worker that
  accepted it. A WebSocket does not migrate between processes, and the design
  says so rather than pretending otherwise.

**As built (N4).** The library is `stdlib/websocket.bas`, about 700 lines, and
the native part is three small things. Where it differs from the draft:

- **The upgrade is a response, not a verb.** A route returns
  `websocket.accept(req)`, a response record with `upgrade: true` and status
  101; the webserver writes the 101 and hands the descriptor, its TLS session
  and any bytes past the request to `net` (`net_adopt_upgrade`), where it is
  an `accepted` event carrying the request and no listener. As a response it
  goes through the same validation and the same two delivery paths (the
  `server` block's handler and the `responses` queue) as any other, and a
  route stays a function that returns a response. **The draft's premise was
  wrong:** "today an upgrade is refused, by design" -- no code in the
  webserver mentioned an upgrade at all (read, not run), so a request asking
  for one reached its route as an ordinary GET.
- **`websocket.events(e)` is a conversion, not a second queue.** A library
  cannot install a watcher, and the program already watches `net.events`, so
  one `net` event is turned into the WebSocket events it means. What framing
  needs between events (the handshake a client awaits, a message's fragments)
  is kept with the connection by a new generic `net.set_state(c, name,
  value)`, read as `c.state.name`.
- **Two core builtins**, `bytes_xor` (masking; a per-byte gBASIC loop over a
  frame is too slow) and `valid_utf8` (strict RFC 3629, which the RFC's 1007
  needs and the lenient string builtins do not answer). Both are checked
  against Python.
- **No idle timeout on an upgraded connection** unless `idle:` asks: a quiet
  WebSocket is ordinary. Pings are answered automatically and reported.
- Interop is measured against Python's `websockets` package both ways, plus
  raw-socket peers for the violations a conforming library will not send.
  Extensions (`permessage-deflate`) are not offered.

## 7. Errors

Raised with code 1004 (an I/O-style operation failure), source `net`, and a
stable `details.code`, so a program catches by code rather than by matching
text:

| `details.code` | meaning |
|---|---|
| `refused` | nothing is listening there (ECONNREFUSED) |
| `timeout` | the connect, read or flush did not finish in time |
| `reset` | the peer reset the connection (ECONNRESET, EPIPE) |
| `closed` | end of stream before what was asked for arrived (`details.partial`) |
| `unreachable` | no route to the network or host |
| `name_not_found` | the host name does not resolve |
| `address_in_use` | `listen` on a port another socket holds |
| `permission` | a port below 1024 without the privilege, or a path's mode |
| `tls_verify` | the certificate did not verify (`details.reason`) |
| `tls_failed` | the handshake failed for another reason |
| `backpressure` | a write past the send ceiling |
| `too_long` | a delimiter or length past the receive ceiling |
| `limit` | `max_connections` reached (a listener event, not a raise) |

The same code rides the `error` event, so a watched connection and a waited one
report a failure identically.

## 8. Binary data: `pack` and `unpack`

Every non-text protocol is integers at fixed widths in a fixed byte order. SSH
is big-endian `uint32` lengths and `mpint`s, and PostgreSQL is big-endian
`int32`/`int16` with a type byte. gBASIC has `to_bytes`/`from_bytes`, `byte_at`
and 32-bit bitwise operators, which can do it one byte at a time, which is the
off-by-one factory P4 names. So, two core builtins:

```basic
frame = pack(">B I", [ 81, 4 + byte_count(sql) + 1 ]) + sql + chr(0)
v = unpack(">I H", header)          ' -> [ length, count ]
v = unpack(">I", buf, 4)            ' at a byte offset
```

- **Format letters:** `B b` (8-bit unsigned/signed), `H h` (16), `I i` (32),
  `Q q` (64, exact through gBASIC's exact integers), `s` (a string with an
  explicit count, `5s`), `x` (a pad byte). Byte order is `>` (big, network)
  or `<` (little), with **no default**. A missing order is refused, because a
  default byte order is a silent bug on half the protocols.
- **A value that does not fit its width is refused**, naming the field. 300 in
  a `B` is never wrapped.
- `pack_size(format)` gives the byte count, for a header's fixed part.

These are general builtins, not `net`'s: the xlsx and serialize code have the
same need in C, and finio's fixed-width formats in gBASIC.

## 9. What an SSH server and client need beyond this

Networking gives SSH its transport and nothing else. SSH brings its own
cryptography, so these are separate gaps, listed so the Transward work can be
sized honestly:

| need | for | exists? |
|---|---|---|
| TCP client and server, binary framing | the transport | **this design** (§3, §8) |
| X25519 key exchange | `curve25519-sha256`, the KEX every modern peer offers | **no**, libcrypto has it |
| Ed25519 sign/verify | `ssh-ed25519` host and user keys | yes |
| SHA-256/512, HMAC-SHA-256/512 | the exchange hash, key derivation, `hmac-sha2-*-etm` | yes |
| AES-256-GCM with a caller's nonce | `aes256-gcm@openssh.com` | yes; check the nonce/AAD shape fits (4-byte length as AAD) |
| ChaCha20 and Poly1305, raw | `chacha20-poly1305@openssh.com`, OpenSSH's default | **no**, libcrypto has both |
| AES-CTR | `aes*-ctr` with ETM MACs, for older peers | **no** |
| RSA verify (and sign) | `rsa-sha2-256/512` keys, still common on servers and in `authorized_keys` | **no** |
| ECDSA P-256 | `ecdsa-sha2-nistp256` host keys | **no** |
| OpenSSH key file formats | reading `id_ed25519`, `authorized_keys`, `known_hosts` | **no**, but pure gBASIC over base64 and `unpack` |
| a pseudo-terminal | a server's interactive session (`forkpty`, window size) | **no**: `process` has pipes only |
| raw terminal mode and `SIGWINCH` | an interactive client | **no** for programs; the prompt's line editor does it privately |
| `process` attached to a pty | the shell a session runs | **no** |

None of these is large on its own. The pty is the only one with design
questions (how a session's child is reaped, how window size travels). A minimal
interoperable SSH needs X25519, Ed25519, AES-GCM or ChaCha20-Poly1305, and the
pty. RSA host-key verification is what a client needs to reach the servers that
exist. I would cost it as its own design once networking is in.

## 10. What a networked RDBMS needs beyond this

| need | for | exists? |
|---|---|---|
| TCP and Unix listeners, many connections, `start_tls` | the server | **this design** |
| peer credentials | local authentication by OS user | **this design** (§3) |
| SCRAM-SHA-256 | password authentication without sending the password | yes: `pbkdf2_sha256`, `hmac_sha256`, `sha256` |
| **a wire protocol** | how clients talk to it | **a decision**: see below |
| concurrent readers | many users, one report not stalling them | **no**: the G-SQL owner is single-threaded (gsql-scale direction, "parallel readers still to design") |

**The wire-protocol decision is the one that shapes the project.** Speaking
**PostgreSQL's v3 protocol** would let `psql`, every PostgreSQL driver
(JDBC, ODBC, Python, Go) and every BI tool connect without anything written
for gBASIC. That protocol is documented, stable, and framed exactly the way §3
and §8 make easy. Its cost is the parts of PostgreSQL's catalog those clients
query on connect (`pg_type`, `version()`, `SET` parameters), which a
compatibility layer answers. A gBASIC-only protocol is simpler and reaches
nobody. I would recommend PostgreSQL's, and decide it in its own design.

Neither database question changes this design. Both confirm `start_tls`, Unix
listeners and peer credentials belong in the first increments rather than
later.

## 11. Increments

Each ships with its own suite, self-checking, run headless against loopback
peers, with Python's `socket` and `ssl` modules as the independent other end
(a peer this module did not write), and under valgrind and `--track-fds`.

| # | increment | contents |
|---|---|---|
| N1 | TCP and Unix sockets | `net.connect`, `net.listen`, accept, both families of reads, the write queue, backpressure, idle and connect timeouts, `max_connections`, events, peer credentials, named errors, `pack`/`unpack`. Loopback default. The resolver thread. |
| N2 | TLS | client verify on by default, SNI, mTLS, server TLS sharing webserver's loading, `start_tls`, peer certificate. Certificates generated per run, as `run_web_tls.sh` does. |
| N3 | UDP | `net.datagram`, `send_to`, `receive`, datagram events, truncation reported. |
| N4 | WebSockets | the `websocket` library (client), the webserver's upgrade hand-off (server), interop with Python's `websockets` if present. |
| N5 | handing a connection to a worker | an accepted connection passed to an actor over its mailbox (`SCM_RIGHTS`), for multi-process servers. Designed when a server needs it, not before. |

N1 is the bulk and is useful alone: an SSH transport, a PostgreSQL-protocol
listener without TLS, and any line protocol all need only N1.

**What the N1 suite must prove, beyond "it works":**

- **A partial send is the module's:** a slow reader (Python reading one byte at
  a time) still receives every byte of a 10 MiB write, in order.
- **A slow consumer bounds memory:** a fast sender against a program that does
  not `take` reaches `full` and pauses, with resident memory measured flat
  rather than growing with the bytes sent.
- **Framing at every split:** a frame delivered one byte per `send` reassembles
  identically to one delivered whole, for `take_exactly`, `take_until` and
  `take_line`. That is the defect class P4 exists for.
- **A thousand connections at once** are accepted, echoed and closed with no
  descriptor left open (`--track-fds`).
- **The loop under Qt:** a socket echo keeps answering while a modal dialog is
  up, using `run_qt_loop`'s method, since that is P1's promise.
- **Each named error is reachable** and carries its code: refused (a closed
  port), timeout (a blackhole address, bounded), reset (Python closes with
  SO_LINGER 0), name_not_found (`.invalid`), address_in_use.
- **NUL and invalid UTF-8 travel untouched**, both ways.

## 12. Portability

POSIX `poll`, sockets and `getaddrinfo` cover Linux, the BSDs and macOS.
Windows has Winsock and `WSAPoll` behind the existing platform layer
(`src/platform_win32.c`). Unix sockets exist on Windows 10 and later, but
peer credentials do not, so `peer_credentials` answers `unknown` there. Qt's
`QSocketNotifier` works on sockets only on Windows, which suits this module,
since everything it adds is a socket.

## 13. Questions, ruled by Matthew 2026-11-04 (every recommendation taken)

1. **The module's name.** `net` (as here), or `socket`/`tcp`? `net` covers TCP,
   Unix and UDP under one word.
2. **A blocking read inside a watcher: warn, as `http.wait` does, or refuse?**
   I recommend warn, for consistency with `http`. It is the same mistake with
   the same remedy.
3. **WebSockets as a gBASIC library over `net` (§6)**, rather than C. I
   recommend the library.
4. **`pack`/`unpack` as core builtins (§8)**, or inside `net`? I recommend core,
   since files and fixed-width formats want them too.
5. **The RDBMS wire protocol:** PostgreSQL v3 compatibility (recommended, §10)
   or gBASIC's own. It is not needed to build this design, only to know it is
   the target.
6. **Windows in N1, or after?** I recommend after: N1 on Linux, the BSDs and
   macOS, with Windows through the platform layer once the surface has settled.
