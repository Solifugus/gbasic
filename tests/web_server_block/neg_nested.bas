' A server block inside a function has no frame to bind in and no
' registration moment -- refused at load time, not at some later reach.
function f()
    server inner( port: 1 )
    end server
    return 0
end function
