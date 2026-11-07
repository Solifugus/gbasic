' Streamable HTTP (Transward §7.4): `mcp.handle_http` behind a `server` block,
' run by run_mcp.sh's streamable tier. The application authenticates each
' request itself (a bearer token, as Transward does), builds the caller from
' that and from `mcp.session_of`, and hands the rest to the library.
'
' WORKERS: 2 IS THE POINT. A session table would live in one worker's memory,
' so a session issued by one worker would be refused by the other; a signed
' session id is checked by whichever worker gets the request.
load mcp
load tools

function whoami(args)
    return principal()
end function

server rpc( port: 0 )

    post "/mcp"( req )
        return answer(req)
    end post

    get "/mcp"( req )
        return answer(req)
    end get

    delete "/mcp"( req )
        return answer(req)
    end delete

end server

function answer(req)
    want = "Bearer " + env("MCP_TOKEN")
    got = ""
    if has(req.headers, "authorization") then
        got = req.headers["authorization"]
    end if
    if got != want then
        return { status: 401, body: "" }
    end if
    ppid = process.run({ command: "sh", args: ["-c", "echo $PPID"] })
    caller = { user: "token-holder", client: "", worker: trim(ppid.stdout) }
    s = mcp.session_of(OPTS, req)
    if s != nothing then
        caller.client = s.client
    end if
    return mcp.handle_http(SRV, OPTS, req, caller)
end function

program main( args )
    ts = tools.define("bank", [
        { name: "whoami", describe: "Report the principal.", params: [], reads: [], mutates: [], fn: whoami }
    ])
    SRV = mcp.server(ts, {})
    OPTS = { name: "bank-mcp", version: "1.0.0", origins: ["https://app.example"],
             session_secret: env("MCP_SECRET") }
    cfg = web.configure(rpc, { port: number(env("MCP_HTTP_PORT")), workers: 2 })
    h = web.serve(cfg)
end program
