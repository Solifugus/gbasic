' WP-MDA-1 — mdna.sections extraction over two REAL consecutive-year 10-Ks of one
' filer (Crocs Inc., CIK 1334036: FY2025 and FY2024). For each year it prints the
' first ~12 words of the MD&A (Item 7) and Risk Factors (Item 1A) sections for an
' eyeball check against the filing, then a headerless synthetic HTML proves the
' best-effort failure path returns `unknown` per section.
program main(args)
    load mdna from "../stdlib/mdna.bas"

    fixtures = ["tenk_crox_2025_sample.htm", "tenk_crox_2024_sample.htm"]
    y = 0
    while y < count(fixtures)
        pref(file)= "examples/fixtures/edgar/" + fixtures[y]
        html = join(read_lines(pref), "\n")
        sec = mdna.sections(html)
        print("== " + fixtures[y] + " ==")
        print("mdna [" + string(len(sec["mdna"])) + " chars]: " + first_words(sec["mdna"], 12))
        print("risk [" + string(len(sec["risk_factors"])) + " chars]: " + first_words(sec["risk_factors"], 12))
        y = y + 1
    end while

    ' failure path: headerless HTML -> unknown per section (best effort)
    bad = "<html><body><p>Quarterly musings with no Item headings whatsoever.</p></body></html>"
    b = mdna.sections(bad)
    print("== headerless synthetic ==")
    print("mdna unknown? " + string(is_unknown(b["mdna"])))
    print("risk unknown? " + string(is_unknown(b["risk_factors"])))
    print("whole-doc fallback text len=" + string(len(mdna.text(bad))))
end program

' First n whitespace-delimited words of s (newlines flattened to spaces first).
function first_words(s, n)
    flat = replace(s, chr(10), " ")
    parts = split(flat, " ")
    out = ""
    i = 0
    k = 0
    while i < count(parts) and k < n
        if parts[i] != "" then
            if out != "" then
                out = out + " "
            end if
            out = out + parts[i]
            k = k + 1
        end if
        i = i + 1
    end while
    return out
end function
