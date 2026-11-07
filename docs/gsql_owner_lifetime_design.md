# G-SQL: how long an owner lives

**Status:** Shipped 2026-11-01, Matthew's ruling the same day (`src/gsql/owner.c`, `src/gsql/client.c`; the lifetime tiers of `run_gsql_owner.sh` and `run_gsql_module.sh`). What building it changed is §6. It replaces the owner lifetime in `gsql_owner_design.md` §6 and `gsql_module_design.md` §3.

## 1. The defect

An owner is started by the first program to connect, as that program's child, armed with `PR_SET_PDEATHSIG`. It ends when that program ends, and only then. Two things follow, and neither is what a database should do:

- **A second program loses its connection when the first one exits.** Program A starts the owner and program B connects to it. When A ends, the owner gets `SIGTERM`, and B's next request fails with `LOST` even though B did nothing wrong.
- **An owner nobody uses lives as long as its starter.** A program that opened a database, closed it, and went on with other work keeps the owner, and the database's lock, until it exits. `gsql_backup_design.md` recorded the cost: a copy a program has opened cannot be refreshed until that program ends, even after it has closed the copy.

## 2. The rule

**An owner lives while it has connections, and for a short linger after the last one closes. It is detached from the program that started it.**

- **Linger.** Once the last connection closes, the owner waits `linger` (3 seconds by default). A connection made within that time keeps it. If none comes, it stops with the planned handoff: transactions rolled back, the log flushed, so the next owner replays nothing.
- **The linger also covers starting.** It starts when the owner says `ready`. An owner that is started and never connected to stops on its own.
- **Detached.** The owner is started in a session of its own (`setsid`, then a second fork), so it is not the program's child, and no parent-death signal is armed. When the starter exits, the owner is untouched. A Ctrl-C at the starter's terminal no longer reaches it either.
- **The linger is the starter's choice.** It is passed as `gsql.connect(path, { linger: seconds })`, and `0` means stop as soon as the last connection closes. It is an option of the owner, so only the connection that starts the owner sets it, and a later program's `linger` is ignored. That is stated in the reference rather than made an error, because a program cannot know whether it will be the one that starts the owner.

`reference.md`'s promise that nothing the interpreter starts outlives it gains one exception, named there. A database owner outlives its starter while another program uses it, and for its linger after that.

## 3. Three races, and how each is closed

**A connection arriving as the owner decides to stop.** Before stopping, the owner accepts anything already queued. If something is there, it keeps serving. Otherwise it unlinks its socket and then closes the listener, in that order. A connect that lands between the last accept and the close is reset before the welcome. The client already treats a lost handshake as "start again" (`reach`), and nothing was sent, so nothing is lost or retried.

**A new owner started while the old one is still flushing.** The client finds no socket and starts an owner, which finds the lock held and answers `busy`. Until now, `busy` meant that another owner won the race and is about to bind its socket, so the client waited for that socket. Here the winner is leaving, so no socket will ever appear, and the client would wait out its full deadline. The loser can tell the two cases apart without guessing: **an owner that finds the lock held keeps trying to open while no socket answers**. If the winner is starting, its socket answers and the loser says `busy`. If the winner is leaving, the lock comes free and the loser opens and becomes the owner. Both cases are bounded by the same deadline the client already waits under.

**The stop deadline.** `SIGTERM` still means stop (a person or a supervisor may send it), with the existing `alarm` deadline, so an owner stuck in a statement cannot hold the lock forever.

## 4. What detaching takes with it, and what replaces it

- **Standard streams.** A detached owner's stdin, stdout and stderr are `/dev/null`. An owner still holding the program's pipes would hold a `$(...)` capture open for its whole linger. Everything the owner has to say before it is ready already goes on the ready line. After that, everything goes over the protocol.
- **Exit status before ready.** The starter cannot `waitpid` a grandchild. The intermediate process therefore waits for the ready line itself. If the owner exits first, the intermediate reports the status as an `error` line (`exited with status 127`). The client's diagnostics are unchanged.
- **The owner's pid.** The ready line becomes `ready PID`, so `gsql_client_owner_pid` still names it.

## 5. Tests

- **The defect, as a difference:** program A starts the owner, program B connects, and A exits. B's next request must succeed, with the same owner pid. The control: the same arrangement on the old build loses B's request.
- **Linger:** after the last close, the owner is still there within the linger and gone after it, and the next open replays nothing (the planned handoff). With `linger: 0`, the owner is gone at once. A program reconnecting within the linger reaches the same owner.
- **Never connected:** an owner started and then left alone stops on its own.
- **Busy while leaving:** start an owner with a linger of 0, close, and immediately reconnect, many times in a loop. Each reconnect must succeed and take far less than the client's wait deadline.
- **The starter killed with SIGKILL** while another program is connected: the other program is unaffected. Once it closes, the owner stops after its linger.
- **No pipes held:** `$(gbasic prog)` returns as soon as the program does, not after the linger.
- **Leftovers:** no owner outlives the suite, waiting for linger plus a margin.
- **The backup tier** keeps reading its copy from a child program. A refresh in the same program after closing with `linger: 0` was measured by hand (§6).

## 6. What building it changed and found

- **An owner waits for its own starter.** With no linger, an owner said `ready` and found itself alone before the client that started it had connected, so it stopped, and the client started another, which stopped the same way: an endless loop of owners, found by the churn test hanging. Before its first connection an owner waits at least 2 s, whatever its linger.
- **The leaving window had to be held open on purpose.** A loop of opens against owners with no linger never met an owner that was still leaving, because starting a process takes longer than the old owner takes to let go of the lock. So removing the wait for the lock left everything green. The test now holds the lock in process with no socket, which is exactly an owner leaving, and requires an open to succeed within 2 s. Without the wait it takes 10 026 ms, the client's whole wait.
- **That test's first draft hung on the correct build.** The process that opens was forked while the test held the lock, so it inherited the lock's descriptor and kept the lock after the test let go. It is forked first now.
- **A parent-death signal armed in the intermediate process is not a perturbation.** It is not inherited across `fork`, so the owner stayed detached and everything stayed green. Restoring the old arrangement itself, with the owner exec'd as the client's own child and armed, reddens both suites' lifetime tiers and names the defect: "the starter's death cost another program its connection".
- **A copy can be refreshed in the program that opened it.** Opened with `{ linger: 0 }` and closed, its owner stops at once, and a refresh straight after succeeds (measured). This closes the limit `gsql_backup_design.md` §6 recorded.
- **What is still unbounded:** an owner stuck in a statement cannot notice its last connection going. It used to die with its starter; now it waits until a `SIGTERM`, which still ends it within its deadline. G-SQL's long statements are sliced (`gsql_slicing_design.md`), so an owner goes back to its loop between slices. Only a statement with no end could hold an owner, and none is known in G-SQL's SQL; that is reasoned, not measured.

**Perturbations proven red:** the owner as the client's child again, the linger ignored, no wait for a leaving owner's lock, the program's streams kept, and no startup grace.

## 7. gdash's G20-15: busy, and then gone (2026-11-07)

gdash saw an open stall about 10 s under concurrent connects at linger 0. The whole of one such stall was sampled every 20 ms (lock, socket, owner processes): for 10.2 s the lock was free, no socket existed and no owner was running. The client was waiting for nobody. Three things were wrong, each found by measuring after the one before was fixed:

- **The client waited 10 s for an owner already gone.** An owner says `busy` only once the winner's socket has answered (§3). At linger 0 the winner can serve its own client and leave within milliseconds of that answer. The client then retried a socket that no longer existed until its deadline. Now the client waits only for a socket that is there and too busy to accept (`EAGAIN`, a full backlog). Anything else means no owner is there, and it starts again at once.
- **A connect and close counted as an owner's first client.** The `busy` check is a connect and close on the winner's socket. The owner marked itself served on any accept, so a check that reached a new owner before its starter ended the 2 s startup grace (§6). At linger 0 the owner then stopped before the client that started it could connect. An owner is served now once a client says HELLO. Found by the churn tier once the first fix was in: about 3% of opens failed, and every attempt read "busy", then "connection refused".
- **Five attempts was a count, not a bound.** Each lost attempt had cost up to 10 s, so the cap was never reached. With starts failing fast, a client could lose five in a row (one open in 8,000). The client now tries `ATTEMPTS` times at once, then every 20 ms until its wait has passed.

**Tests** (`run_gsql_owner.sh`):

- `busygone` makes each case happen. A test owner relays the real owner's ready line and holds a `busy` back while the winner goes. It probes a new owner before relaying `ready`. It answers `busy` six times with no owner behind it.
- Reverting each fix turns its part red 3 runs in 3. The client fix is the only one that shows as 10 020 ms.
- `churn`: eight clients open, ping and close 100 times each at linger 0, and no open may take 2 s. On the old build it failed 5 runs in 6, with the slowest open at 10 048 to 30 091 ms. On the fixed build 16,000 opens over 20 runs were all served, the slowest in 118 ms.
- Churn alone could not hold the fixes. With the owner fix in, reverting the client fix turned it red 1 run in 6, and reverting the owner fix 0 in 6, so each fix has its own deterministic part.

**Not measured:** the wait on `EAGAIN` (a full backlog) has no test that fills a backlog. The window tier's 4 s-a-sync case (§3, measured at 80 s before this) was not re-run.
