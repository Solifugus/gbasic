' A library for tests/run_profile.sh: its line must be reported under its own
' file, and its function under the name a caller writes.
library countlib
    function triple(x)
        return x * 3
    end function
end library
