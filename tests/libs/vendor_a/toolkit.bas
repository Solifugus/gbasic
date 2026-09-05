' TWO VENDORS, ONE NAME. This library and tests/libs/vendor_b/toolkit.bas both
' declare `library toolkit` -- the situation a library name alias exists for.
' They share `describe` and `format` so a merged import would be caught by the
' duplicate-function refusal, and they DISAGREE about what those return so a
' test can tell which one it actually reached.
library toolkit
    function describe()
        return "vendor A"
    end function
    function format(x)
        return "A:" + string(x)
    end function
    function only_a()
        return "a-only"
    end function
end library
