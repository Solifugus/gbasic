' xlsx.try_open — a batch tool that survives a bad workbook.
'
' The gap (DOGFOOD 2026-08-03): `xlsx.open` raises, gBASIC cannot catch a raise,
' so ONE malformed workbook in thousands aborted an entire corpus scan and
' yielded no data at all. The 15,871-workbook Enron scan had to be driven from a
' shell loop, one gbasic process per file, purely to contain that.
'
' This is the `try_decode` shape: report the failure as a VALUE.
'
'   r = xlsx.try_open(path)      ' { ok, workbook, message }
'
' The property that matters is not that try_open exists -- it is that the two
' forms cannot DISAGREE. A `try_` twin that accepts a file its raising sibling
' rejects, or reports a different reason for the same file, is worse than none:
' it invites you to trust a verdict the real function does not share. They run
' one code path, and the parity block below is what holds that.
program main(args)
    scratch = args[0]

    ' A corpus with a hole in it, built here so the test carries no binary.
    ' `copy` moves the bytes; read/write would not -- a workbook is binary.
    src{file} = "examples/fixtures/xlsx/basic.xlsx"
    good{file} = scratch + "/good.xlsx"
    copy(src, good)
    naz{file} = scratch + "/notazip.xlsx"
    write(naz, "PK-ish but not really")
    trunc{file} = scratch + "/truncated.xlsx"
    write(trunc, "PK\u{0003}\u{0004} then nothing that follows it")
    emp{file} = scratch + "/empty.xlsx"
    write(emp, "")

    ' --- the batch survives every one of them ---------------------------
    names = sort(["good.xlsx", "truncated.xlsx", "notazip.xlsx", "empty.xlsx"])
    ok = 0
    bad = 0
    for each n in names
        r = xlsx.try_open(scratch + "/" + n)
        if r.ok then
            ok = ok + 1
            print "readable   " + n + "  sheets=" + string(len(xlsx.sheets(r.workbook)))
        else
            bad = bad + 1
            print "unreadable " + n
        end if
    end for
    print "batch finished: readable=" + string(ok) + " unreadable=" + string(bad)

    ' --- the failure record's shape -------------------------------------
    r = xlsx.try_open(scratch + "/notazip.xlsx")
    print "ok field:       " + string(r.ok)
    print "workbook field: " + string(r.workbook)
    print "message empty:  " + string(r.message = "")
    g = xlsx.try_open(scratch + "/good.xlsx")
    print "success ok:     " + string(g.ok)
    print "success msg:    [" + g.message + "]"

    ' --- PARITY: the same verdict, and the same reason, as `open` -------
    ' Only the function's own name differs in a message that names it, which is
    ' correct: each reports as itself.
    for each n in names
        t = xlsx.try_open(scratch + "/" + n)
        print n + " -> " + string(t.ok) + " | " + replace(t.message, "try_open", "open")
    end for

    ' --- a recursive walk, in-process ------------------------------------
    ' `list_files` returns files only and does not recurse, which is what sent
    ' corpus walks to the shell. `list` on a DIRECTORY reference answers
    ' `{name, type}` for every entry including folders, so the caller keeps a
    ' worklist and the whole scan stays in one process.
    make_dir(scratch + "/nested")
    deep{file} = scratch + "/nested/deep.xlsx"
    copy(src, deep)
    pending = [scratch]
    files = []
    while len(pending) > 0
        here = take_last(pending)
        hd{dir} = here
        for each e in sort_by_name(list(hd))
            full = here + "/" + e.name
            if e.type = "folder" then
                pending = append(pending, full)
            else
                files = append(files, full)
            end if
        end for
    end while
    walked = 0
    survived = 0
    for each f in sort(files)
        walked = walked + 1
        if xlsx.try_open(f).ok then
            survived = survived + 1
        end if
    end for
    print "walked " + string(walked) + " files across 2 levels, opened " + string(survived)
end program

' `list` answers in directory order, which is the filesystem's business and not
' stable across machines; the golden needs one order.
function sort_by_name(entries)
    names = []
    for each e in entries
        names = append(names, e.name)
    end for
    out = []
    for each n in sort(names)
        for each e in entries
            if e.name = n then
                out = append(out, e)
            end if
        end for
    end for
    return out
end function
