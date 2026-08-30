server app( port: 8080 )
    get "/x/{id}" ( id = "fallback" )
        return { body: id }
    end get
end server
print "must not run"
