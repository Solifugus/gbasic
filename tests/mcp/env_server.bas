' A stdio MCP server whose one tool answers a variable from its environment,
' for consume_test.bas's check that `env` in a stdio spec reaches the child
' (a token handed down without a command line, Transward's ask).
load mcp
load tools

function token(args)
    return env("MCP_CHILD_TOKEN")
end function

program main( args )
    ts = tools.define("env", [
        { name: "token", describe: "The child's MCP_CHILD_TOKEN.", params: [], reads: [], mutates: [], fn: token }
    ])
    mcp.serve_stdio(ts, { name: "env-mcp", version: "1.0.0" })
end program
