' STU-4 headless driver for the execution-session engine (studio_session).
' Dispatches on args[0] to a scenario and prints a deterministic, path-free
' transcript for golden comparison. args[1] is a scratch directory (never printed).
'
' Every case drives the session exactly as the shell does -- run, then tick until
' the machine leaves an active state -- so the transitions in the goldens are the
' real ones, not a narration.

' ---- source fixtures -------------------------------------------------------

function base_src()
  return "print \"one\"\n\nfunction add(a, b)\n  return a + b\nend function\n\nprint add(2, 3)\n\nprint \"last\"\n"
end function

function err_target_src()
  ' The LAST section divides by zero: the error lands in the target.
  return "print \"one\"\n\nfunction add(a, b)\n  return a + b\nend function\n\nprint 1 / 0\n"
end function

function err_prefix_src()
  ' The FIRST section divides by zero, so running the LAST section fails inside the
  ' replayed prefix and never reaches the target at all. The function declaration in
  ' the middle is load-bearing: STU-3 collapses consecutive plain statements into a
  ' single section, so without a declaration between them the "prefix" and the
  ' "target" would be the same section.
  return "print \"before\"\nprint 1 / 0\n\nfunction f(x)\n  return x\nend function\n\nprint \"target-section\"\n"
end function

function broken_src()
  return "print \"one\"\n\nfunction add(a, b)\n  return a +\n"
end function

function dup_src()
  ' mul() duplicated verbatim -> both candidates go `ambiguous` (STU-3).
  return "print \"one\"\n\nfunction mul(a, b)\n  return a * b\nend function\n\nfunction mul(a, b)\n  return a * b\nend function\n"
end function

function slow_src()
  ' Never finishes on its own: only a stop ends it.
  return "print \"started\"\n\nwhile true\n  sleep(0.05)\nend while\n"
end function

function signal_src()
  ' Kills its own interpreter from a grandchild, so the child dies BY SIGNAL with
  ' no diagnostic of any kind -- the case a status-only path must still handle.
  return "print \"before\"\n\nprocess.run({ command: \"sh\", args: [\"-c\", \"kill -TERM $PPID\"] })\n"
end function

function big_src()
  ' ~90 KB on stdout, past a 64 KB pipe buffer, so a driver that failed to drain
  ' while polling would deadlock instead of finishing.
  return "i = 0\n\nwhile i < 6000\n  print \"line-\" + i + \"-padding-padding\"\n  i = i + 1\nend while\n"
end function

function prog_src()
  ' Sections come from the program BODY, so a byte prefix cuts the block open and
  ' materialization must append `end program`. The inner declaration splits the body
  ' into three sections, so running the FIRST one truncates the block mid-way and the
  ' append is what makes the result parse at all.
  return "program main(args)\n  print \"in-program\"\n\n  function helper(n)\n    return n\n  end function\n\n  print \"second\"\nend program\n"
end function

' ---- helpers ---------------------------------------------------------------

function sections_for(src)
  st = studio_sections.create("doc-1")
  return studio_sections.refresh(st, src)
end function

' Drive the session to completion exactly as the shell's timer does.
function drain(sess)
  guard = 0
  while studio_session.is_active(sess)
    sess = studio_session.tick(sess)
    guard = guard + 1
    if guard > 4000 then
      print "!! drain guard tripped"
      return sess
    end if
    sleep(0.01)
  end while
  return sess
end function

function show(label, sess)
  print "-- " + label
  print studio_session.summary(sess)
  print "transitions: " + studio_session.transitions(sess)
end function

function run_and_show(label, sess, secs, src, sid)
  sess = studio_session.run(sess, secs, src, sid)
  sess = drain(sess)
  sess = studio_session.finalize(sess, secs, src)
  show(label, sess)
  return sess
end function

program main(args)
  load studio_json
  load studio_store
  load studio_sections
  load studio_session

  mode = ""
  if count(args) > 0 then
    mode = args[0]
  end if
  scratch = "/tmp/gbasic_stu4_scratch"
  if count(args) > 1 then
    scratch = args[1]
  end if

  if mode = "clean" then
    src = base_src()
    secs = sections_for(src)
    print "sections=" + count(secs.sections)
    sess = studio_session.create("doc-1", scratch)
    show("initial", sess)
    ' Section 1 has no prefix: its output is exactly its own.
    first = secs.sections[0]
    sess = run_and_show("run section 1", sess, secs, src, first.id)
    ' A later section replays everything above it.
    last = secs.sections[count(secs.sections) - 1]
    sess = run_and_show("run last section", sess, secs, src, last.id)
  end if

  if mode = "err_target" then
    src = err_target_src()
    secs = sections_for(src)
    sess = studio_session.create("doc-1", scratch)
    last = secs.sections[count(secs.sections) - 1]
    sess = run_and_show("error in the target section", sess, secs, src, last.id)
  end if

  if mode = "err_prefix" then
    src = err_prefix_src()
    secs = sections_for(src)
    sess = studio_session.create("doc-1", scratch)
    last = secs.sections[count(secs.sections) - 1]
    sess = run_and_show("error in the replayed prefix", sess, secs, src, last.id)
  end if

  if mode = "outside" then
    ' A diagnostic whose position falls in NO section: the appended `end program`
    ' line sits past every section's range, and an error reported there must
    ' attribute as "outside" rather than being forced into the nearest section.
    src = prog_src()
    secs = sections_for(src)
    sess = studio_session.create("doc-1", scratch)
    last = secs.sections[count(secs.sections) - 1]
    sess = studio_session.run(sess, secs, src, last.id)
    sess = drain(sess)
    ' Synthesize a diagnostic beyond the end of the document and attribute it.
    sess.stderr_raw = "{\"severity\":\"error\",\"code\":\"GB_DIAG_RUNTIME_ERROR\",\"subcode\":1002,\"path\":\"x\",\"start\":{\"line\":999,\"column\":1},\"end\":{\"line\":999,\"column\":1},\"message\":\"synthetic out-of-range\"}"
    sess = studio_session.finalize(sess, secs, src)
    show("diagnostic outside every section", sess)
  end if

  if mode = "prog" then
    src = prog_src()
    secs = sections_for(src)
    first = secs.sections[0]
    m = studio_session.materialize_text(src, first)
    print "appended=" + m.appended
    print "materialized=<" + m.text + ">"
    sess = studio_session.create("doc-1", scratch)
    sess = run_and_show("run inside a program block", sess, secs, src, first.id)
  end if

  if mode = "stop" then
    src = slow_src()
    secs = sections_for(src)
    sess = studio_session.create("doc-1", scratch)
    last = secs.sections[count(secs.sections) - 1]
    sess = studio_session.run(sess, secs, src, last.id)
    ' Tick until the child has actually produced something, so the stop lands on a
    ' running child rather than racing its startup.
    ' Settle: the child is already exec'd (process.start verified that), so a few
    ' ticks simply let the state machine observe it running. We deliberately do NOT
    ' wait for output -- a gBASIC child's stdout is BLOCK-buffered on a pipe, so a
    ' program that never exits may never flush a short line at all.
    i = 0
    while i < 5
      sess = studio_session.tick(sess)
      i = i + 1
      sleep(0.01)
    end while
    print "running-before-stop=" + (sess.state = "running")
    sess = studio_session.request_stop(sess)
    sess = drain(sess)
    sess = studio_session.finalize(sess, secs, src)
    show("stopped politely", sess)
  end if

  if mode = "force" then
    src = slow_src()
    secs = sections_for(src)
    sess = studio_session.create("doc-1", scratch)
    last = secs.sections[count(secs.sections) - 1]
    sess = studio_session.run(sess, secs, src, last.id)
    ' Settle: the child is already exec'd (process.start verified that), so a few
    ' ticks simply let the state machine observe it running. We deliberately do NOT
    ' wait for output -- a gBASIC child's stdout is BLOCK-buffered on a pipe, so a
    ' program that never exits may never flush a short line at all.
    i = 0
    while i < 5
      sess = studio_session.tick(sess)
      i = i + 1
      sleep(0.01)
    end while
    sess = studio_session.force_stop(sess, 1)
    sess = studio_session.finalize(sess, secs, src)
    show("force-stopped", sess)
  end if

  if mode = "unresponsive" then
    ' A child that IGNORES SIGTERM must surface as a distinct state, never as a
    ' hang. A gBASIC child always dies on SIGTERM (it installs no handler unless
    ' `with lock` is used, and that one _exits), so the session is pointed at a
    ' helper that really does trap and ignore it. `interpreter` is a session field
    ' precisely so the runner is substitutable; the state machine under test is
    ' identical either way.
    src = slow_src()
    secs = sections_for(src)
    sess = studio_session.create("doc-1", scratch)
    sess.interpreter = "tests/native_platform/helpers/proc_ignore_term.sh"
    sess.stop_grace_ticks = 3
    last = secs.sections[count(secs.sections) - 1]
    sess = studio_session.run(sess, secs, src, last.id)

    ' Settle: the child is already exec'd (process.start verified that), so a few
    ' ticks simply let the state machine observe it running. We deliberately do NOT
    ' wait for output -- a gBASIC child's stdout is BLOCK-buffered on a pipe, so a
    ' program that never exits may never flush a short line at all.
    i = 0
    while i < 5
      sess = studio_session.tick(sess)
      i = i + 1
      sleep(0.01)
    end while

    ' A bare stop cannot end this child.
    sess = studio_session.request_stop(sess)
    print "state-after-polite-stop=" + sess.state
    guard = 0
    while sess.state = "stopping"
      sess = studio_session.tick(sess)
      guard = guard + 1
      if guard > 4000 then
        print "!! never became unresponsive"
        return
      end if
      sleep(0.01)
    end while
    print "state-after-grace=" + sess.state

    ' Escalation is a separate, explicit action.
    sess = studio_session.force_stop(sess, 1)
    sess = studio_session.finalize(sess, secs, src)
    print "state=" + sess.state + " signal=" + sess.signal
    print "transitions: " + studio_session.transitions(sess)
  end if

  if mode = "restart" then
    ' A restart requested MID-RUN must wait for the stop before starting again, so
    ' two runs of one session never overlap.
    src = slow_src()
    secs = sections_for(src)
    sess = studio_session.create("doc-1", scratch)
    last = secs.sections[count(secs.sections) - 1]
    sess = studio_session.run(sess, secs, src, last.id)
    ' Settle: the child is already exec'd (process.start verified that), so a few
    ' ticks simply let the state machine observe it running. We deliberately do NOT
    ' wait for output -- a gBASIC child's stdout is BLOCK-buffered on a pipe, so a
    ' program that never exits may never flush a short line at all.
    i = 0
    while i < 5
      sess = studio_session.tick(sess)
      i = i + 1
      sleep(0.01)
    end while
    print "active-before-restart=" + studio_session.is_active(sess)
    ' Restart onto a different, terminating document so the case ends.
    src2 = base_src()
    secs2 = sections_for(src2)
    first2 = secs2.sections[0]
    sess = studio_session.restart(sess, secs2, src2, first2.id, 1)
    print "run_seq-after-restart=" + sess.run_seq
    sess = drain(sess)
    sess = studio_session.finalize(sess, secs2, src2)
    show("restarted mid-run", sess)
  end if

  if mode = "refuse" then
    sess = studio_session.create("doc-1", scratch)

    ' (1) source does not parse -- last-known-good must not be executed
    good = base_src()
    secs = sections_for(good)
    target = secs.sections[0].id
    secs = studio_sections.refresh(secs, broken_src())
    sess = studio_session.run(sess, secs, broken_src(), target)
    show("refuse: source does not parse", sess)

    ' (2) ambiguous section
    dsrc = dup_src()
    dsecs = sections_for(dsrc)
    dsecs = studio_sections.refresh(dsecs, dsrc)
    amb = ""
    for each s in dsecs.sections
      if s.status = "ambiguous" then
        amb = s.id
      end if
    end for
    print "found-ambiguous=" + (amb != "")
    sess2 = studio_session.create("doc-2", scratch)
    sess2 = studio_session.run(sess2, dsecs, dsrc, amb)
    show("refuse: ambiguous section", sess2)

    ' (3) stale / removed section
    ssrc = base_src()
    ssecs = sections_for(ssrc)
    doomed = ssecs.sections[count(ssecs.sections) - 1].id
    shorter = "print \"one\"\n"
    ssecs = studio_sections.refresh(ssecs, shorter)
    sess3 = studio_session.create("doc-3", scratch)
    sess3 = studio_session.run(sess3, ssecs, shorter, doomed)
    show("refuse: stale section", sess3)
  end if

  if mode = "signal" then
    src = signal_src()
    secs = sections_for(src)
    sess = studio_session.create("doc-1", scratch)
    last = secs.sections[count(secs.sections) - 1]
    sess = run_and_show("child killed by signal", sess, secs, src, last.id)
    print "diagnostics=" + count(sess.diagnostics)
  end if

  if mode = "big" then
    src = big_src()
    secs = sections_for(src)
    sess = studio_session.create("doc-1", scratch)
    last = secs.sections[count(secs.sections) - 1]
    sess = studio_session.run(sess, secs, src, last.id)
    sess = drain(sess)
    sess = studio_session.finalize(sess, secs, src)
    total = byte_count(sess.out_prefix) + byte_count(sess.out_target)
    print "state=" + sess.state
    print "exit_code=" + sess.exit_code
    print "output_bytes=" + total
    print "exceeded_pipe_buffer=" + (total > 65536)
  end if

  if mode = "edited" then
    ' The document is edited between two runs, so the materialized prefix differs.
    src1 = base_src()
    secs1 = sections_for(src1)
    sess = studio_session.create("doc-1", scratch)
    last1 = secs1.sections[count(secs1.sections) - 1]
    m1 = studio_session.materialize_text(src1, last1)
    print "run1_prefix_bytes=" + byte_count(m1.text)
    sess = run_and_show("run 1", sess, secs1, src1, last1.id)

    src2 = "print \"one\"\n\nfunction add(a, b)\n  return a + b\nend function\n\nprint add(2, 3)\n\nprint \"CHANGED\"\n"
    secs2 = studio_sections.refresh(secs1, src2)
    last2 = secs2.sections[count(secs2.sections) - 1]
    m2 = studio_session.materialize_text(src2, last2)
    print "run2_prefix_bytes=" + byte_count(m2.text)
    print "same_section_id=" + (last1.id = last2.id)
    sess = run_and_show("run 2 after edit", sess, secs2, src2, last2.id)
  end if

  if mode = "scratch" then
    ' Scratch lifecycle: a finished run leaves nothing behind, and a sweep clears
    ' whatever a crashed Studio did leave.
    studio_store.ensure_dir(scratch)
    d(dir) = scratch
    src = base_src()
    secs = sections_for(src)
    sess = studio_session.create("doc-1", scratch)
    first = secs.sections[0]
    sess = studio_session.run(sess, secs, src, first.id)
    print "file_exists_during_run=" + (sess.prefix_path != "")
    sess = drain(sess)
    print "prefix_path_after_finish=<" + sess.prefix_path + ">"
    print "files_left_after_run=" + count(list(d))

    ' Simulate a crashed Studio: two orphaned prefixes.
    a(file) = scratch + "/run-doc-9-1.bas"
    b(file) = scratch + "/run-doc-9-2.bas"
    write(a, "print 1\n")
    write(b, "print 2\n")
    print "orphans_before_sweep=" + count(list(d))
    n = studio_session.sweep_scratch(scratch)
    print "swept=" + n
    print "files_after_sweep=" + count(list(d))
  end if
end program
