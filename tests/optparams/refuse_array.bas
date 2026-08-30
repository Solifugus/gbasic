' Only LITERALS may be defaults. An array is mutable and would raise the
' shared-default question this design exists to avoid.
function bad(a, b = [])
    return a
end function
