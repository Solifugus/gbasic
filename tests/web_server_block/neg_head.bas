' Head refusals: a wrong-kind option value, an unknown option, a COMPUTED
' option value (head options are literals -- that rule is what keeps every
' other check statically decidable), a duplicated server name, a mismatched
' closer, and an unknown declarative-block word.
server h( port: "eighty", magic: 1 )
    get "/"( req )
        return 0
    end get
end server

server h( port: 1 )
end server

server computed( port: 1 + 2 )
end server

server c( port: 1 )
end lopsided

observer o( port: 1 )
end observer
