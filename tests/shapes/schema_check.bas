' The gBASIC half of run_shapes.sh's schema tier (docs/shapes_design.md §6.2).
' `schema` prints json_schema(everything); `convert FILE` reads one JSON
' document a line and says, for each, whether it went into the shape with
' nothing refused and nothing warned (`on warning stop`): "ok", or "refused"
' and why. tests/shapes/schema_oracle.py decides which documents the schema
' admits; every one it admits must be "ok" here.
program main(args)
    on warning stop
    if args[0] = "schema" then
        print json_encode(json_schema(everything))
    else
        for each line in {file}args[1]
            print convert(line)
        end for
    end if
end program

function convert(line)
    on error goto next
    x:everything = json_decode(line)
    e = error
    if e then
        return "refused " + replace(e.message, chr(10), " ")
    end if
    return "ok"
end function

shape part
    label:text
    on_day:date = "2000-01-01"
end shape

shape everything
    n:number
    b:boolean
    d:date
    dt:datetime
    t:time
    dur:duration
    usd:money(USD)
    jpy:money(JPY)
    bhd:money(BHD)
    clf:money(CLF)
    s:text
    anything = nothing
    inner:part
    later:date = "2001-02-03"
end shape
