load web

function h(req)
    return { body: "x" }
end function

routes = web.routes([{ method: "GTE", path: "/", handler: h }])
