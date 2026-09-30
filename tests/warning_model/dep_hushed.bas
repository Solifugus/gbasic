' Declares IGNORE, which is NOT the default. That is the whole point of this
' fixture: control 1 originally used a library declaring `print`, and print IS
' the default -- so "the library's mode was honoured" and "there was no
' declaration anywhere" produced identical output and the control passed on a
' build that never honoured a library's mode at all. Found by perturbation.
library dep_hushed
	function work()
		on warning ignore
		warning("the library chose to be quiet about this")
		return 1
	end function
end library
