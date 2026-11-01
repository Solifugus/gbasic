# G-SQL: how long an owner lives

**Status:** Designed 2026-11-01, Matthew's ruling the same day. It replaces the owner lifetime in `gsql_owner_design.md` §6 and `gsql_module_design.md` §3.

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
- **The backup tier** refreshes a copy after closing it, waiting out a linger of 0, instead of needing a child program.
