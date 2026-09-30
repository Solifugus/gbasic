' The same dependency with NO declaration -- the control that separates "a
' library cannot soften the program's policy" from "library warnings are
' special".
library dep_quiet
	function work()
		warning("from a library with no declaration")
		return 1
	end function
end library
