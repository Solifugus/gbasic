' Recipe 1 — A chart is a STRING. One call, one complete SVG document.
'
' No image library, no canvas object, no renderer to configure: chart.line_xy
' takes two lists and returns SVG markup as an ordinary string. Print it,
' serve it from a WebServer handler, or write it to a file and open it in any
' browser. The same call produces the identical bytes every run -- charts are
' golden-file testable like everything else.

program main(args)
    load chart from "../../stdlib/chart.bas"

    svg = chart.line_xy([1, 2, 3, 4, 5], [3, 5, 4, 7, 6])

    print left(svg, 44)
    print string(len(svg)) + " chars of SVG in total"

    f (file)= "examples/chart_cookbook/tmp_first.svg"
    write(f, svg)
    print "saved: " + string(bytes(f)) + " bytes"
    delete(f)
end program
