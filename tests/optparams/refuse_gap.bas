' An optional parameter may not be followed by a required one.
function bad(a = 1, b)
    return a
end function
print "must not run"
