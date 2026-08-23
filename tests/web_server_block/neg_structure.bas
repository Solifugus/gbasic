' Structural load-time refusals: root twice, an unknown directive, an unknown
' hook, trust_proxy inside a site, a duplicated host, a duplicated site name,
' and a site with no host at all.
server s( port: 1 )
    root "a"
    root "b"
    banana "x"
    on reload
        print 1
    end on
    web one( host: "h.example" )
        trust_proxy "1.2.3.4"
    end web
    web one( host: "h.example" )
        get "/"( req )
            return 0
        end get
    end web
    web three( )
        get "/"( req )
            return 0
        end get
    end web
end server
