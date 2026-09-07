' The stdio side of the HTTP tier's comparison: the SAME toolset and options as
' tests/mcp/http_server.bas, served over stdio. The two replies must be
' byte-identical, which is what "two transports, one dispatcher" means.
load mcp
load tools

function get_balance(args)
    return "42.00 for " + args.id
end function

program main( args )
    ts = tools.define("bank", [
        { name: "get_balance", describe: "Read an account balance.",
          params: [ { name: "id", type: "string", describe: "the account id", required: true } ],
          reads: ["accounts"], mutates: [], fn: get_balance }
    ])
    mcp.serve_stdio(ts, { name: "bank-mcp", version: "1.0.0" })
end program
