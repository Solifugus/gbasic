' An MCP server over stdio. Launched by tests/run_mcp.sh both WITH and WITHOUT
' `--line-buffered`, because the difference between the two is a deadlock and
' the suite demonstrates it rather than asserting it.
load mcp
load tools

function get_balance(args)
    return "42.00 for " + args.id
end function

function whoami(args)
    p = principal()
    if p = nothing then return "nobody"
    return p.user
end function

function boom(args)
    error "the tool raised"
    return "unreached"
end function

program main( args )
    ' Built INSIDE the program block: a top-level statement does not run when
    ' one exists, so a toolset assembled up there is simply not there.
    ts = tools.define("bank", [
        { name: "get_balance", describe: "Read an account balance.",
          params: [ { name: "id", type: "string", describe: "the account id", required: true } ],
          reads: ["accounts"], mutates: [], fn: get_balance },
        { name: "whoami", describe: "Report the principal calls are answered for.",
          params: [], reads: [], mutates: [], fn: whoami },
        { name: "boom", describe: "Always fails.",
          params: [], reads: [], mutates: [], fn: boom }
    ])

    opts = { name: "bank-mcp", version: "1.0.0" }
    if env("MCP_PRINCIPAL") != "" then
        opts = { name: "bank-mcp", version: "1.0.0",
                 principal: { user: env("MCP_PRINCIPAL"), groups: ["tellers"] },
                 max_groups: 2 }
    end if
    mcp.serve_stdio(ts, opts)
end program
