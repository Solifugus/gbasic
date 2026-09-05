' Two definitions of one name inside ONE library. `function_find_local` cannot
' see this -- a library's functions are all `imported` -- so it needs its own
' lookup, and without one this stayed silent after the file-scope case was fixed.
library duplicate_fn
    function helper()
        return "first"
    end function

    function helper()
        return "second"
    end function
end library
