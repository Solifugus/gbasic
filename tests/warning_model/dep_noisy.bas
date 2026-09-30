' A dependency that declares its OWN warning mode. Nothing in stdlib does this
' (measured 2026-09-30: the single mention in finance.bas is a comment), so the
' case a third-party library will create has to be built here.
library dep_noisy
	function work()
		on warning print
		warning("something is off")
		return 1
	end function
end library
