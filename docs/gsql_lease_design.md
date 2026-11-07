# G-SQL: one owner per database on any storage

**Status:** Shipped 2026-11-01 (`src/gsql/lease.c`; the lease tier of `tests/run_gsql_owner.sh`, and the crash gate of `run_gsql_store.sh`, which now crashes the lease's own writes). What building it changed is §9. It is item 4 of the work order in `gsql_scale_measurements.md` §4, from Matthew's direction the same day that large databases live on NAS storage whose kind cannot be known in advance (*"we need to be flexible and adaptive"*). It closes the limit `gsql_practices.md` §5 records: *one owner per database is enforced on one machine only*.

## 1. The gap

One owner per database is G-SQL's whole concurrency model (`gsql_owner_design.md` §6). It rests on one fact: **the engine takes an exclusive `flock` on `DB.lock` when it opens, and an owner that does not get it is not the owner.**

`flock` means what it says on a local filesystem. On shared storage it depends on the filesystem and on how it was mounted:

- **NFSv4** on Linux: `flock` is emulated with byte-range locks, and those are held by the server, so two machines do exclude each other.
- **NFS mounted `nolock`**, or NFSv3 without a lock daemon: each machine's lock is local, and two machines both get it.
- **CIFS/SMB**: `flock` is local to the client by default.
- **FUSE** filesystems: whatever the filesystem implements, which is often nothing.

The program cannot tell which one it is on. In the failing cases, two machines each start an owner on one database. Each replays the same log, writes into it and replaces the same manifest. **That corrupts the database, and nothing reports it until a later open finds the damage.**

## 2. The rule

**An owner holds a lease, kept in the database's own directory, and renews it once a second. Another machine takes the database only after watching the lease stand still for long enough that its owner must be gone.**

- **Liveness is judged by movement, never by a clock reading.** The lease carries a counter the owner increments at every renewal. An opener reads it repeatedly and times it with **its own monotonic clock**. No timestamp written by one machine is ever compared with another machine's clock, so clock skew between machines, and a clock stepped by NTP, change nothing.
- **The same machine needs no wait.** If the lease names this machine and this boot, the `flock` we already hold is authoritative here, so the previous owner is gone. The lease is taken at once.
- **A clean close leaves no lease.** The next open, from any machine, takes it at once. A crash and restart on one machine, which is the common case and what owner lifetime churns through (`gsql_owner_lifetime_design.md`), costs nothing new.
- **Another machine's lease costs a wait.** An opener watches for `dead_ms` (12 seconds). If the counter moves at any point, the database is in use elsewhere and the open is refused at once, naming the machine. An owner that is alive therefore produces a refusal within about a second. **An owner that crashed on another machine makes the next open from a different machine wait 12 seconds, and then 2 more to settle.** That is the price of not trusting clocks, and it is paid only after a crash, and only by another machine.

## 3. The lease file

`DB.lease`, beside `DB.lock`, is one 512-byte sector, overwritten in place:

| field | bytes | meaning |
|---|---|---|
| magic `GSQLLEAS`, format 1 | 12 | |
| token | 16 | random, drawn by each owner when it opens |
| counter | 8 | incremented at every renewal |
| machine | 32 | `/etc/machine-id` (or `/var/lib/dbus/machine-id`, or the host name) |
| boot | 16 | `/proc/sys/kernel/random/boot_id`; zero where there is none |
| pid | 8 | for messages only |
| host name | 64 | for messages only |
| zero padding, then a CRC-32C | to 512 | |

**One sector, overwritten in place,** rather than a whole file renamed into place, because a renewal happens every second in the owner's single thread. An in-place sector write followed by `fdatasync` is the cheapest durable write there is. A torn or half-visible sector fails its checksum, and an observer counts that as **movement**, which is the safe direction: it can only delay a takeover, never cause one.

**The machine and the boot both decide "this machine".** A machine id alone is not enough, because cloned virtual machines can share one. Clones booted separately have different boot ids, so they are treated as different machines and wait. Where there is no boot id (outside Linux), the fast path is never taken, and every takeover waits.

**Every read opens the file afresh.** NFS clients cache file data and attributes, the latter for up to a minute. A program that kept the file open and re-read it could see the same counter for a minute while the owner renewed it, and take over a live database. Opening the file revalidates it with the server (close-to-open consistency). A renewal is also open, write, `fdatasync`, close, so the close pushes the write to the server.

## 4. Taking a lease

After the engine has the `flock` and before it reads the manifest or the log:

1. **If there is no lease file, create it** with our record, **exclusively**: the create fails if the file exists, and that is atomic on a local filesystem, NFSv3 and later, and SMB. A create that succeeds holds the lease, with no wait. One that fails means another opener has just created it; go on as if it had been there. Go to step 5.
2. **If the record names this machine and this boot, overwrite it at once.** The `flock` we hold rules out a running owner on this machine, so this is a restart after a crash here.
3. **Otherwise watch it** for `dead_ms`, reading every `renew_ms / 4`. That covers a record naming another machine, and one that cannot be read (an empty file, a bad checksum, a damaged sector). If the counter, the token or the checksum changes at any point, refuse: **the database is open on another machine**. One that stays unreadable for `dead_ms` is damage, not an owner.
4. **Write our record**, re-reading the lease immediately before; if it changed since the last read, refuse as in step 3. Then **settle**: wait `settle_ms` (2 seconds) and read it once more. If it is not our token, another machine wrote after us and won; refuse.
5. Open the manifest and replay the log.

**A clean close removes the lease file**, after reading it to confirm it still holds our token. So a planned handoff takes no wait on any machine, and step 1 is the path every open takes after a clean close: owner lifetime's churn costs one create and one removal per owner, nothing more.

**Two openers from different machines** that both watched a dead lease both write in step 4. The sector holds the last write, so after the settle exactly one finds its own token, and the other refuses. The re-read before writing makes the window between them a few milliseconds, against a settle of 2 seconds. The store is opened only after the settle, so the loser has written nothing but the lease.

**Step 2 does not settle,** and it does not need to. Its only possible rival is another machine that has watched our machine's dead record for `dead_ms` and is about to write. If our write lands first, the rival's re-read sees it move and refuses. If the rival writes inside the few milliseconds between its re-read and its write, it holds the sector, our owner finds a foreign token at its next renewal, within `renew_ms`, and stops, while the rival is still settling and has not yet opened the store. Whatever our owner committed in between is in the log the rival then replays. That is the same margin argument as the fence (§5).

## 5. Keeping it, and the fence

**Renewal.** The owner renews from its poll loop every `renew_ms` (1 second). Each renewal reads the lease first. **If the token is not ours, the database has been taken: the owner stops at once.** It writes nothing more, not even the planned handoff's flush, and exits. Its clients' requests fail with `LOST`, and their next connect starts an owner that is refused with `elsewhere`, naming the machine that took it. It does not flush, since the flush is a write.

**The fence.** A renewal can be late: the owner may be busy in a long statement, or the whole machine may have been paused. So the engine itself checks, before every write another owner could see (a log append, a manifest replace): **if the last confirmed renewal is older than `trust_ms` (4 seconds), renew first, synchronously.** A renewal that finds a foreign token fails the write, and the store stops accepting any (`GS_FENCED`).

**The timings are a margin, and the margin is stated.** An observer takes over no sooner than `dead_ms` after the last renewal it saw. An owner writes only within `trust_ms` of a renewal it confirmed. **A write that started within its trust window and finishes within `dead_ms − trust_ms` (8 seconds) is safe.** A write that stalls for longer between its check and its completion, because the process was stopped or the storage hung, can land after another machine took over. Closing that would need the storage itself to refuse a stale owner's writes (a fencing token checked by the server), which a file on NFS or SMB cannot do. **This is the residual risk, and it is the standard one for leases on storage that cannot fence.**

| timing | default | why |
|---|---|---|
| `renew_ms` | 1 000 | one durable sector write a second |
| `trust_ms` | 4 000 | an owner busy for up to 4 seconds between renewals writes without renewing first |
| `dead_ms` | 12 000 | three times `trust_ms`, the 8-second margin above |
| `settle_ms` | 2 000 | far longer than step 3's re-read window |

## 6. What it costs

- **A renewal a second:** an open, a 512-byte write, an `fdatasync` and a close, in the owner's thread. **Measured on NVMe ext4: a median of 264 µs, p99 462 µs**, about 0.03% of the owner's time. **On NFS it is not measured here** (there is no network storage on this machine); it is a round trip to the server for the open, the write and the sync, and while it runs the owner serves nobody. It is reported as a statistic (`GsStats.lease_renewals`, `lease_renew_us_max`) so it can be measured where it matters.
- **A takeover from another machine:** 14 seconds after a crash. After a clean close there is no lease, and nothing to wait for.
- **On storage where `flock` already works across machines,** the lease adds the renewal and nothing else. The program cannot tell which storage it is on, which is the whole point.

## 7. Decisions

- **The lease is always on.** It is cheap where storage is local, and an option would be one somebody turns off on exactly the storage that needs it.
- **It is the engine's**, through the I/O layer, so the simulated layer and the crash harness reach it, and every program that opens a store gets the same rule. **A backup's destination is not covered:** a backup takes the destination's lock and no lease, so two machines backing up into one directory on such storage are not kept apart.
- **A new status, `GS_ELSEWHERE`,** for "the database is open on another machine", distinct from `GS_BUSY`. The owner's `busy` answer means "another owner on this machine is starting, wait for its socket", and a socket on another machine never answers, so a client that took this for `busy` would wait its whole deadline. The ready line is `elsewhere HOST PID`, and the client's error names the machine.
- **`GS_FENCED`** for a store that has lost its lease. Every later call that writes answers it.
- **A test-only identity.** Two machines cannot be had in a test. `GsOptions` gains a machine identity to use instead of the real one, and a flag that makes the `flock` local to that identity (the lock file is named after it), which is exactly the storage this design is for. The four timings can be scaled down the same way, so a takeover test waits about a second rather than fourteen. Only the C interface sets any of these; `gbasic --gsql-owner` never does.

## 8. Tests

- **The gap itself, as a difference:** two owners with different test identities and local locks on one database. Without the lease, both open (shown by the control, the lease turned off by a perturbation). With it, the second is refused with `elsewhere`, naming the first, **within about one renewal**, not after `dead_ms`.
- **Takeover after a crash:** SIGKILL the first owner. The second, from another identity, waits at least `dead_ms` and then opens, and sees every committed row.
- **The same machine:** a SIGKILLed owner's restart, same identity, opens at once (well under `dead_ms`).
- **A clean close:** the lease file is gone, and another identity then opens at once.
- **Two creators:** two identities opening an absent lease at once, many rounds: exactly one creates it, and the other is refused.
- **Theft:** an owner whose lease is overwritten by another identity's record stops; its next write is refused with `GS_FENCED`, and its clients get `LOST` naming the other machine.
- **The fence without renewal:** an owner whose poll loop is blocked (a long statement) past `trust_ms` renews before its next commit, and if the lease was taken meanwhile, the commit is refused.
- **Two contenders** from different identities over a dead lease: exactly one wins, every time, over many rounds.
- **A torn lease** (a corrupted sector) counts as movement: an observer does not take over while it sees one.
- **The crash harness:** the lease's writes are crashed like every other write, and every recovery's checks still hold. Two machines are not modelled there; that is the lease tier's.
- **Cost:** the renewal's time is reported, and the point-read cost tier of `run_gsql_owner.sh` must not move.

## 9. What building it changed and found

- **Time belongs to the I/O layer.** The first crash-gate run did not finish in ten minutes: a crash can leave the lease created and not yet written, an unreadable lease is watched for `dead_ms`, and every such recovery waited twelve real seconds. Time is input from the environment, as random bytes already were (`gsql_io_random`), so `gsql_io_now_ms` and `gsql_io_sleep_ms` are the layer's: CLOCK_MONOTONIC for the real one, and for the simulated one a virtual clock that only sleeping moves. The crash gate then ran in 3 minutes, against 2.5 before the lease.
- **The fence is a layer, not a list of call sites.** The store writes log records, manifests, sorted files, blob files and spare logs from a dozen places, and a stale owner writing any of them corrupts the new owner's files, since both number files from the same counter. So the store writes through a wrapper of the I/O layer that checks the lease before every write, truncation, rename, removal and create. A write path added later cannot forget to ask.
- **A lease file must be named.** The store owns its directory and deletes at open whatever it does not name. The lease and a test's per-machine lock file are named beside `DB.lock`.
- **The contended checks were wrong twice before they could fail.** First, a winner let go of its owner at once, so a second machine taking the database afterwards, legitimately, counted as a second winner. Second, the race they meant to provoke is microseconds wide, and with the exclusive create or the settle's check removed they stayed green over six rounds each. Each contender now holds its owner until both have answered, and a test-only pause between deciding and writing (`GsLeaseOptions.race_ms`) makes both decide before either writes. Both perturbations then fail every round.
- **Treating damage as movement is a second layer, not the only one.** With a damaged sector's changes ignored during the watch, an opener is still refused while something keeps writing the sector: the re-read before its write, or the settle's final read, sees the change. The watch's check is kept, since it refuses sooner, but no test can show it is needed.
- **A pre-existing flaky check, found by this work's runs:** the owner-lifetime tier's churn floor of 20 owners in 40 opens measured 13 and 18 on the build that introduced it. It is 3 now; the deterministic "leaving" tier is what holds that window open.

**Perturbations proven red:** never watching, no fence, a renewal that ignores a foreign token, a close that leaves the lease, no fast path on the same machine, a non-exclusive create, and no check after the settle. Each is caught by the check written for it.

**Not measured:** NFS, SMB and FUSE themselves, since this machine has none. The lease is tested with pretend machines (their own socket name, identity and local lock) on one ext4 filesystem, which reproduces local-only locking exactly and close-to-open caching not at all.
