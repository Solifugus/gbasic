' Loaded at the TOP LEVEL by plat_guard_prereg_actor.bas. Deliberately depends
' on nothing: if it pulled in the block-position library the fixture could not
' tell the two positions apart, which is exactly the mistake the first version
' of that fixture made -- `alias_host` loads `alias_dep`, so removing the
' block-position hoist changed nothing and the tier passed on a broken build.
library prereg_top
    function tag()
        return "top"
    end function
end library
