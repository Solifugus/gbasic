' The SAME dispatcher behind an HTTP `server` block. That is the claim §1.11
' makes -- two transports, one `mcp.handle` -- and this is what tests it: a
' JSON-RPC request posted over HTTP must get the identical reply the stdio
' transport gives for the same request.
load mcp
load tools

function get_balance(args)
    return "42.00 for " + args.id
end function

server rpc( port: 0 )

    post "/rpc"( req )
        return { body: mcp.handle(TOOLSET, OPTS, req.body),
                 headers: { "Content-Type": "application/json" } }
    end post

end server

program main( args )
    TOOLSET = tools.define("bank", [
        { name: "get_balance", describe: "Read an account balance.",
          params: [ { name: "id", type: "string", describe: "the account id", required: true } ],
          reads: ["accounts"], mutates: [], fn: get_balance }
    ])
    OPTS = { name: "bank-mcp", version: "1.0.0" }
    cfg = web.configure(rpc, { port: number(env("MCP_HTTP_PORT")) })
    h = web.serve(cfg)
end program
