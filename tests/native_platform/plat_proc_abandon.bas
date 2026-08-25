' PLAT-PROC: handles ABANDONED without release must not leak an fd or leave a
' zombie. Two loops cover both shapes:
'   A. start -> stop -> abandon   (child already dead; must have been reaped)
'   B. start -> abandon           (child still RUNNING; fds must close anyway)
' Abandonment is expressed by overwriting the only variable holding the handle, so
' the last reference goes away with no explicit release.
'
' Measurement is taken from the interpreter's own /proc: a child `sh` sees the
' gbasic process as $PPID, so it can count our open descriptors and our zombie
' children. Both counts are compared as DELTAS against a baseline taken the same
' way, so the transient fds of the measuring process.run itself cancel out.
'
' Each child also records its own pid in a file the RUNNER inspects after this
' program exits, to prove that teardown left none of them behind.

function open_fds()
  r = process.run({ command: "sh", args: ["-c", "ls /proc/$PPID/fd | wc -l"] })
  return number(trim(r.stdout))
end function

function zombie_children()
  r = process.run({ command: "sh", args: ["-c", "ps -o stat= --ppid $PPID | grep -c Z || true"] })
  return number(trim(r.stdout))
end function

pidfile = "/tmp/gbasic_plat_proc_abandon.pids"
pf{file} = pidfile
if exists(pf) then
  delete(pf)
end if

base_fds = open_fds()

i = 0
while i < 25
  h = process.start({ command: "tests/native_platform/helpers/proc_abandon_child.sh", args: [pidfile] })
  process.stop(h, { force_after: 5 })
  h = 0
  i = i + 1
end while
print "A_fd_delta=" + (open_fds() - base_fds)
print "A_zombies=" + zombie_children()

i = 0
while i < 25
  h = process.start({ command: "tests/native_platform/helpers/proc_abandon_child.sh", args: [pidfile] })
  h = 0
  i = i + 1
end while
print "B_fd_delta=" + (open_fds() - base_fds)
print "B_zombies=" + zombie_children()
