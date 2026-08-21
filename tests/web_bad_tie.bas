load web

function h(req)
    return { body: "x" }
end function

routes = web.routes([{ method: "get", path: "/a/{x}", handler: h }, { method: "get", path: "/a/{y}", handler: h }])
