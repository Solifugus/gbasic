' Every route-shaped load-time refusal from design §8, in one file: the
' validation pass reports them ALL (it does not stop at the first), so one
' parse exercises the whole list.
server bad( port: 1 )
    fetch "/x"( req )
        return 0
    end fetch
    get "/a/{id}/{id}"( req )
        return 0
    end get
    get "/a/{id}"( req )
        return 0
    end get
    get "/a/{name}"( req )
        return 0
    end get
    get "/b"( req )
        return 0
    end get
    get "/b"( req )
        return 0
    end get
    get "unrooted"( req )
        return 0
    end get
    get "/c/{x...}/tail"( req )
        return 0
    end get
    stream "/b"( req )
        return 0
    end stream
end server
