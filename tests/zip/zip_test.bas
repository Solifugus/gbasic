' The zip module's refusals and zip.extract (gap G11 from the gBASIC books),
' over the archives tests/make_zip_cases.py makes. Run by tests/run_zip.sh
' from inside the folder holding them; every answer is written here by hand.
checks = { n: 0, bad: 0 }

function check(label, got, want)
    checks.n = checks.n + 1
    if type(got) = type(want) then
        if got = want then
            return nothing
        end if
    end if
    checks.bad = checks.bad + 1
    print "MISMATCH " + label + ": got " + type(got) + " " + string(got) + ", want " + type(want) + " " + string(want)
    return nothing
end function

' the message a call was refused with, or "ok"
function refusal_of_read(z, name)
    on error goto refused
    r = zip.read(z, name)
    return "ok"
refused:
    m = error.message
    error.clear()
    return m
end function

function refusal_of_extract(z, dir, opts)
    on error goto refused
    r = zip.extract(z, dir, opts)
    return "ok"
refused:
    m = error.message
    error.clear()
    return m
end function

function refusal_of_members(z)
    on error goto refused
    r = zip.members(z)
    return "ok"
refused:
    m = error.message
    error.clear()
    return m
end function

' --- what members says ------------------------------------------------------------
ms = zip.members({file}"plain.zip")
check("plain.zip has six members", count(ms), 6)
check("  stored", ms[0].method, "store")
check("  the folder is a folder", ms[1].folder, true)
check("  packed is smaller for a compressible member", ms[2].packed < ms[2].size, true)
check("  modified is the DOS time zipfile wrote", ms[0].modified, {datetime}"2026-11-05 09:30:12")
check("  nothing is a link or encrypted", ms[0].link or ms[0].encrypted, false)
check("a link is listed as one", zip.members({file}"link.zip")[0].link, true)
check("bzip2 is listed by name", zip.members({file}"bzip2.zip")[0].method, "bzip2")
check("an encrypted member is listed as one", zip.members({file}"enc.zip")[0].encrypted, true)
check("a path as text works too", count(zip.members("plain.zip")), 6)

' --- reading -----------------------------------------------------------------------
check("an empty member reads as empty", zip.read({file}"plain.zip", "empty.bin"), "")
check("NUL bytes survive", len(zip.read({file}"plain.zip", "nul.bin")), 6)
check("no such member", refusal_of_read({file}"plain.zip", "nope.txt"),
      "zip.read: plain.zip has no member named \"nope.txt\" (zip.members lists them)")
check("a folder has no bytes", refusal_of_read({file}"plain.zip", "data/"),
      "zip.read: \"data/\" in plain.zip is a folder, which has no bytes")
check("two members of one name", refusal_of_read({file}"dup.zip", "same.txt"),
      "zip.read: dup.zip holds two members named \"same.txt\", and which one is meant cannot be told")
check("bzip2", refusal_of_read({file}"bzip2.zip", "b.txt"),
      "zip.read: \"b.txt\" in bzip2.zip is compressed with bzip2 (method 12); only store and deflate are supported")
check("a wrong CRC-32", refusal_of_read({file}"crc.zip", "c.txt"),
      "zip.read: crc.zip is damaged: \"c.txt\" does not match its CRC-32")
check("encrypted", refusal_of_read({file}"enc.zip", "e.txt"),
      "zip.read: \"e.txt\" in enc.zip is encrypted, which is not supported")
check("not a ZIP file", refusal_of_members({file}"trunc.zip"),
      "zip.members: trunc.zip is not a ZIP file (it has no end-of-central-directory record)")
check("cut inside its comment", refusal_of_members({file}"short.zip"),
      "zip.members: short.zip is damaged: it ends inside its own archive comment (was it cut short?)")
check("a folder given as a file", refusal_of_extract({file}"plain.zip", {file}"out", {}),
      "zip.extract expects a folder to write into, such as {dir}\"incoming\"")

' --- extracting --------------------------------------------------------------------
make_dir({dir}"out")
got = zip.extract({file}"plain.zip", {dir}"out")
check("extract answers the files written, folders made on the way", count(got), 5)
check("  the bytes are the member's", read({file}"out/data/invoices.csv") = zip.read({file}"plain.zip",
      "data/invoices.csv"), true)
check("  a UTF-8 name is a UTF-8 file name", exists({file}"out/data/café.txt"), true)
check("a second extract refuses to write over a file", refusal_of_extract({file}"plain.zip", {dir}"out", {}),
      "zip.extract: out/readme.txt is already there (replace: true writes over it)")
check("  replace: true writes over it", count(zip.extract({file}"plain.zip", {dir}"out", { replace: true })), 5)
check("an option it does not have", refusal_of_extract({file}"plain.zip", {dir}"out", { overwrite: true }),
      "zip.extract: no option named overwrite (the one option is replace)")
check("a folder that is not there", refusal_of_extract({file}"plain.zip", {dir}"nowhere", {}),
      "zip.extract: the folder nowhere does not exist (make_dir makes one)")
make_dir({dir}"slip")
check("a name climbing out with ..", refusal_of_extract({file}"slip.zip", {dir}"slip", {}),
      "zip.extract: slip.zip holds \"../evil.txt\", which cannot be written: it climbs out with ..; nothing was extracted")
check("  and nothing was written, not even the safe member before it", exists({file}"slip/fine.txt"), false)
check("  nor the evil one", exists({file}"evil.txt"), false)
check("an absolute name", refusal_of_extract({file}"abs.zip", {dir}"slip", {}),
      "zip.extract: abs.zip holds \"/etc/evil_\", which cannot be written: it is an absolute path; nothing was extracted")
check("a link", refusal_of_extract({file}"link.zip", {dir}"slip", {}),
      "zip.extract: link.zip holds \"link\", which cannot be written: it is a symbolic link; nothing was extracted")
check("two members of one name", refusal_of_extract({file}"dup.zip", {dir}"slip", {}),
      "zip.extract: dup.zip holds two members named \"same.txt\"; nothing was extracted")

print "checks: " + string(checks.n)
print "mismatches: " + string(checks.bad)
