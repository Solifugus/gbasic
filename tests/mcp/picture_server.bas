' An MCP server over stdio whose one tool answers content blocks, text and an
' image (docs/qt_gui_design.md §0.38 D2), for tests/mcp/consume_test.bas: the
' image's bytes hold a NUL and bytes past 0x7F, so a decode that stopped early
' or read text would not give them back.
load mcp
load tools

function picture(args)
    return mcp.content([mcp.text("a picture"), mcp.image(from_bytes([137, 80, 78, 71, 0, 255, 10, 13]), "image/png")])
end function

program main( args )
    ts = tools.define("pictures", [
        { name: "picture", describe: "Answer a picture.",
          params: [], reads: [], mutates: [], fn: picture } ])
    mcp.serve_stdio(ts, { name: "pictures", version: "1.0.0" })
end program
