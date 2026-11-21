' A library for tests/run_profile.sh: its line must be reported under its own
' file, and its function under the name a caller writes.
library countlib
    function triple(x)
        return x * 3
    end function

    ' Calls back into the program: the callee is the program's function,
    ' defined in the program's file, though first called from here.
    function call_back(f)
        return f()
    end function
end library
