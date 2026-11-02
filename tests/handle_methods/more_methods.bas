' The other handle kinds take their module's verbs as methods too
' (docs/handle_methods_design.md): sqlite, an xml reader, a process, an http
' handle and an xlsx workbook, each asked beside its module form. The runner
' skips this fixture, saying so, on a build that lacks one of these modules.
load sqlite
load xml
load http

function check(what, got, want)
    if string(got) = string(want) and type(got) = type(want) then
        print "ok " + what
    else
        print "MISMATCH " + what + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

function sqlite_part(dir)
    db = sqlite.connect(join_path(dir, "m.db"))
    r = db.exec("create table t (n integer)")
    r = db.exec("insert into t values (?), (?)", [1, 2])
    check("sqlite: a connection's query", len(db.query("select n from t")), 2)
    check("sqlite: the module form agrees", string(sqlite.query(db, "select n from t")), string(db.query("select n from t")))
    c = db.close()
    return nothing
end function

function xml_part(dir)
    f = join_path(dir, "m.xml")
    w = write({file}f, "<a><b>1</b><b>2</b></a>")
    rd = xml.reader(f)
    got = []
    while rd.skip_to("b")
        append(got, xml.text(rd.subtree()))
    end while
    check("xml: a reader's skip_to and subtree walk the file", join(got, ","), "1,2")
    c = rd.close()
    return nothing
end function

function process_part()
    h = process.start({ command: "sh", args: ["-c", "exit 3"] })
    s = h.wait()
    check("process: a handle's wait", s.exit_code, 3)
    check("process: the module form agrees", process.poll(h).exit_code, h.poll().exit_code)
    r = h.release()
    return nothing
end function

function http_part()
    ' nothing listens on port 9 here, so the transfer fails without a network
    h = http.start({ url: "http://127.0.0.1:9/", timeout: 5 })
    s = h.wait()
    check("http: a handle's wait", s.transport_ok, false)
    check("http: the module form agrees", http.poll(h).transport_ok, h.poll().transport_ok)
    r = h.release()
    return nothing
end function

function xlsx_part()
    wb = xlsx.open("examples/fixtures/xlsx/basic.xlsx")
    check("xlsx: a workbook's sheets", join(wb.sheets(), ","), join(xlsx.sheets(wb), ","))
    return nothing
end function

program main(args)
    dir = args[0]
    sqlite_part(dir)
    xml_part(dir)
    process_part()
    http_part()
    xlsx_part()
    print "done"
end program
