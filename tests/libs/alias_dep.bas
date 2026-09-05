' The dependency half of the alias test: `alias_host` loads THIS library and
' calls it qualified. Aliasing the host must not rename what the host loads.
library alias_dep
    function tag()
        return "dep"
    end function
end library
