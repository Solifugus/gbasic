' Loaded from INSIDE the program block by plat_guard_prereg_actor.bas, and
' reachable from NOWHERE else -- no other library loads it, so the child can
' only see it if the block position is hoisted. See prereg_top.bas.
library prereg_block
    function tag()
        return "block"
    end function
end library
