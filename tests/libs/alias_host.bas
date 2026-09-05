' A library with an internal unqualified call, a qualified call into a
' dependency, and an exported modifier -- the three things that must keep
' working when the LOADER renames this library.
library alias_host
    load alias_dep from "alias_dep.bas"

    function helper()
        return "host helper"
    end function

    function outer()
        ' Unqualified: must reach THIS library's own helper, whatever name the
        ' loading file gave this library.
        return helper()
    end function

    function through_dep()
        ' Qualified into a dependency: the dependency keeps its OWN name, since
        ' an alias is the importing file's word for one library and not a
        ' renaming that reaches into what that library loads.
        return alias_dep.tag()
    end function

    export modifier shouted for assign
        return upper(value) + "!"
    end modifier
end library
