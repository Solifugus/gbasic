' WP-XML-7 — lenient HTML: xml.parse_html + whole-document xml.text extraction.
' Two checks: (1) a REAL 10-K/A (inline-XBRL HTML, EDGAR's modern annual-report
' format — examples/fixtures/edgar/tenk_10ka_sample.htm, CIK 2070900) parses and
' its extracted visible text contains three sentinel strings read from the cover
' page; (2) deliberately broken tag soup (unclosed h1/p/b/i, no </html>) parses
' WITHOUT error and still yields text — libxml2's HTML parser repairs rather than
' rejects. xml.text is document order with no layout, so block text concatenates.
program main(args)
    load xml

    ' --- (1) real 10-K/A ---
    p(file)= "examples/fixtures/edgar/tenk_10ka_sample.htm"
    src = join(read_lines(p), "\n")
    doc = xml.parse_html(src)
    print("root=" + doc["name"])
    txt = xml.text(doc)

    report_sentinel("sentinel1", txt, "SECURITIES AND EXCHANGE COMMISSION")
    report_sentinel("sentinel2", txt, "For the fiscal year ended March 31, 2026")
    report_sentinel("sentinel3", txt, "Emerging growth company")

    ' --- (2) tag soup: unclosed tags, missing </html> ---
    soup = "<html><body><h1>Title<p>Para one<b>bold<i>italic</body>"
    d2 = xml.parse_html(soup)
    print("soup_root=" + d2["name"])
    print("soup_text=" + xml.text(d2))
end program

function report_sentinel(label, haystack, needle)
    if is_nothing(find(haystack, needle)) then
        print(label + "=MISSING")
    else
        print(label + "=found")
    end if
end function
