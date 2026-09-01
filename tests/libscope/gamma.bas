library gamma
    ' Defines no `helper`, so an unqualified call must still reach the global
    ' table -- the control that keeps the fix from sealing libraries off.
    function reach()
        return helper()
    end function
end library
