' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' websocket -- RFC 6455 WebSockets, client and server, over `net`.
'
' docs/networking_design.md §6 (N4). Framing is gBASIC over net's take and read
' families; the only native parts are the webserver handing an upgraded socket
' to `net` (a response with `upgrade: true`), `bytes_xor` for masking and
' `valid_utf8`. The handshake needs `sha1`, so this library needs libcrypto in
' the build.
'
' ONE CONVERSION, NOT A SECOND LOOP. A watched WebSocket is an ordinary watched
' `net` connection, and `websocket.events(e)` turns one `net` event into the
' WebSocket events it means (open, message, ping, pong, closed, error), most
' often none or one. The program keeps its one `watch(net.events)`; a library
' cannot install a watcher, and a second event queue would be a second thing
' to drain. What a connection's framing needs between events (the handshake it
' is waiting for, the fragments of a message) is kept with the connection by
' `net.set_state`, under the name "websocket".
'
' WHAT IS REFUSED, AS THE RFC SAYS: a frame with a reserved bit set (no
' extension is negotiated), a client frame that is not masked or a server frame
' that is, a control frame that is fragmented or longer than 125 bytes, a
' continuation with nothing to continue, text that is not UTF-8 (1007), a
' message past max_message (1009), and a close code no endpoint may send. Each
' closes the connection with the code that names it.
library websocket

    load net

    function _guid()
        return "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
    end function

    ' The accept value for a key: base64(sha1(key + GUID)), RFC 6455 §4.2.2.
    function _accept_value(key)
        return base64_encode(sha1(key + _guid()))
    end function

    function _fail_msg(what, reason)
        error { message: "websocket." + what + ": " + reason, source: "websocket", reason: reason }
    end function

    ' "ws://host:port/path?q" -> { tls, host, port, path }
    function _parse_url(url)
        u = url
        tls = false
        if starts_with(lower(u), "wss://") then
            tls = true
            u = _from(u, 6)
        else if starts_with(lower(u), "ws://") then
            u = _from(u, 5)
        else
            error { message: "websocket.connect: the address must begin ws:// or wss://, not " + url,
                    source: "websocket", reason: "bad_url" }
        end if
        slash = find(u, "/")
        path = "/"
        authority = u
        if not is_nothing(slash) then
            path = _from(u, slash)
            authority = left(u, slash)
        end if
        if authority = "" then
            error { message: "websocket.connect: no host in " + url, source: "websocket", reason: "bad_url" }
        end if
        port = 80
        if tls then
            port = 443
        end if
        host = authority
        if starts_with(authority, "[") then
            close_at = find(authority, "]")
            host = mid(authority, 1, close_at - 1)
            if len(authority) > close_at + 1 then
                port = number(_from(authority, close_at + 2))
            end if
        else
            colon = find(authority, ":")
            if not is_nothing(colon) then
                host = left(authority, colon)
                port = number(_from(authority, colon + 1))
            end if
        end if
        return { tls: tls, host: host, port: port, path: path, authority: authority }
    end function

    function _option(options, name, fallback)
        if is_nothing(options) then
            return fallback
        end if
        if has(options, name) then
            return options[name]
        end if
        return fallback
    end function

    function _check_options(options, verb, known)
        if is_nothing(options) then
            return nothing
        end if
        if type(options) != "record" then
            error { message: "websocket." + verb + ": options must be a record", source: "websocket",
                    reason: "bad_option" }
        end if
        for each k in keys(options)
            if not contains(known, k) then
                error { message: "websocket." + verb + ": unknown option '" + k + "' (known: " + join(known, ", ") + ")",
                        source: "websocket", reason: "bad_option" }
            end if
        end for
        return nothing
    end function

    ' ---- frames ---------------------------------------------------------------

    ' One frame's bytes. A client masks with a fresh key per frame (§5.3); a
    ' server never masks.
    function _frame(opcode, payload, masked)
        n = byte_count(payload)
        b1 = 0
        if masked then
            b1 = 128
        end if
        head = ""
        if n < 126 then
            head = pack(">B B", [128 + opcode, b1 + n])
        else if n < 65536 then
            head = pack(">B B H", [128 + opcode, b1 + 126, n])
        else
            head = pack(">B B Q", [128 + opcode, b1 + 127, n])
        end if
        if masked then
            key = random_bytes(4)
            return head + key + bytes_xor(payload, key)
        end if
        return head + payload
    end function

    ' How many bytes the frame whose first two bytes are `two` occupies before
    ' its payload, and whether the length is in those bytes yet: a record
    ' { header, short } where short is the 7-bit length.
    function _header_size(two)
        b = unpack(">B B", two)
        short = mod(b[1], 128)
        size = 2
        if short = 126 then
            size = 4
        else if short = 127 then
            size = 10
        end if
        if b[1] >= 128 then
            size = size + 4
        end if
        return { header: size, short: short, b0: b[0], b1: b[1] }
    end function

    ' A frame's fields from its header bytes: { fin, rsv, opcode, masked,
    ' length, header } (length -1: the 64-bit length had its top bit set).
    function _parse_header(head)
        h = _header_size(_left_bytes(head, 2))
        length = h.short
        if h.short = 126 then
            length = unpack(">H", byte_slice(head, 2, 2))[0]
        else if h.short = 127 then
            hi = unpack(">I", byte_slice(head, 2, 4))[0]
            lo = unpack(">I", byte_slice(head, 6, 4))[0]
            if hi >= 2147483648 then
                length = -1
            else
                length = hi * 4294967296 + lo
            end if
        end if
        return { fin: h.b0 >= 128, rsv: mod(h.b0, 128) >= 16, opcode: mod(h.b0, 16), masked: h.b1 >= 128,
                 length: length, header: h.header }
    end function

    ' Everything from codepoint `at` on (mid needs a length).
    function _from(s, at)
        return mid(s, at, len(s) - at)
    end function

    function _left_bytes(s, n)
        return byte_slice(s, 0, n)
    end function

    ' A close payload: code (2 bytes) then UTF-8 reason.
    function _close_payload(code, reason)
        if is_nothing(code) then
            return ""
        end if
        return pack(">H", [code]) + reason
    end function

    ' Codes an endpoint may put in a close frame (§7.4).
    function _valid_close_code(code)
        if code >= 1000 and code <= 1003 then
            return true
        end if
        if code >= 1007 and code <= 1011 then
            return true
        end if
        return code >= 3000 and code <= 4999
    end function

    function _new_state(role, max_message)
        return { role: role, open: false, expect: "", max_message: max_message, parts: "",
                 part_opcode: 0, sent_close: false, closed: false, protocol: "" }
    end function

    function _state(c)
        st = c.state
        if has(st, "websocket") then
            return st.websocket
        end if
        return nothing
    end function

    ' Fail the connection with `code` (§7.1.7): send a close frame naming it,
    ' close, and answer the closed event the program is told.
    function _fail(c, st, code, reason)
        if not st.sent_close and c.open then
            c.write(_frame(8, _close_payload(code, reason), st.role = "client"))
        end if
        st.sent_close = true
        st.closed = true
        net.set_state(c, "websocket", st)
        if c.open then
            c.close()
        end if
        return { kind: "closed", connection: c, code: code, reason: reason, clean: false }
    end function

    ' Act on one whole frame. Answers a WebSocket event record, or nothing when
    ' the frame was a fragment, and updates st through the connection.
    function _on_frame(c, st, f, payload)
        if f.rsv then
            return _fail(c, st, 1002, "a reserved bit is set and no extension was negotiated")
        end if
        if st.role = "server" and not f.masked then
            return _fail(c, st, 1002, "a client frame must be masked")
        end if
        if st.role = "client" and f.masked then
            return _fail(c, st, 1002, "a server frame must not be masked")
        end if
        op = f.opcode
        if op >= 8 then
            if not f.fin or f.length > 125 then
                return _fail(c, st, 1002, "a control frame must be one frame of at most 125 bytes")
            end if
            if op = 8 then
                code = 1005
                reason = ""
                if byte_count(payload) = 1 then
                    return _fail(c, st, 1002, "a close frame's payload is a two-byte code or nothing")
                end if
                if byte_count(payload) >= 2 then
                    code = unpack(">H", _left_bytes(payload, 2))[0]
                    reason = byte_slice(payload, 2)
                    if not _valid_close_code(code) then
                        return _fail(c, st, 1002, "close code " + string(code) + " may not be sent")
                    end if
                    if not valid_utf8(reason) then
                        return _fail(c, st, 1007, "a close reason must be UTF-8")
                    end if
                end if
                ' the closing handshake: echo it unless we began it, then close
                if not st.sent_close and c.open then
                    echo_code = code
                    if code = 1005 then
                        echo_code = nothing
                    end if
                    c.write(_frame(8, _close_payload(echo_code, ""), st.role = "client"))
                end if
                st.sent_close = true
                st.closed = true
                net.set_state(c, "websocket", st)
                if c.open then
                    c.close()
                end if
                return { kind: "closed", connection: c, code: code, reason: reason, clean: true }
            end if
            if op = 9 then
                c.write(_frame(10, payload, st.role = "client"))
                return { kind: "ping", connection: c, data: payload }
            end if
            if op = 10 then
                return { kind: "pong", connection: c, data: payload }
            end if
            return _fail(c, st, 1002, "opcode " + string(op) + " is reserved")
        end if
        if op = 0 then
            if st.part_opcode = 0 then
                return _fail(c, st, 1002, "a continuation frame with no message to continue")
            end if
        else if op = 1 or op = 2 then
            if st.part_opcode != 0 then
                return _fail(c, st, 1002, "a new message began before the last one finished")
            end if
            st.part_opcode = op
        else
            return _fail(c, st, 1002, "opcode " + string(op) + " is reserved")
        end if
        if byte_count(st.parts) + byte_count(payload) > st.max_message then
            return _fail(c, st, 1009, "the message is longer than max_message (" + string(st.max_message) + " bytes)")
        end if
        st.parts = st.parts + payload
        if not f.fin then
            net.set_state(c, "websocket", st)
            return nothing
        end if
        data = st.parts
        binary = st.part_opcode = 2
        st.parts = ""
        st.part_opcode = 0
        net.set_state(c, "websocket", st)
        if not binary and not valid_utf8(data) then
            return _fail(c, st, 1007, "a text message must be UTF-8")
        end if
        return { kind: "message", connection: c, data: data, binary: binary }
    end function

    ' A frame's payload, unmasked.
    function _payload(f, frame)
        body = byte_slice(frame, f.header)
        if f.masked then
            key = byte_slice(frame, f.header - 4, 4)
            return bytes_xor(body, key)
        end if
        return body
    end function

    ' Every whole frame buffered on a watched connection, as events.
    function _drain_frames(c, st)
        out = []
        while c.open and not st.closed and c.buffered() >= 2
            h = _header_size(c.peek_exactly(2))
            if c.buffered() < h.header then
                return out
            end if
            f = _parse_header(c.peek_exactly(h.header))
            if f.length < 0 or f.length > st.max_message then
                append(out, _fail(c, st, 1009, "a frame is longer than max_message (" + string(st.max_message) + " bytes)"))
                return out
            end if
            if c.buffered() < f.header + f.length then
                return out
            end if
            frame = c.take_exactly(f.header + f.length)
            ev = _on_frame(c, st, f, _payload(f, frame))
            st = _state(c)
            if not is_nothing(ev) then
                append(out, ev)
            end if
        end while
        return out
    end function

    ' The server's answer to a client's handshake: { why, protocol }, why
    ' nothing when it is right.
    function _check_answer(head, st)
        lines = split(head, chr(13) + chr(10))
        if not starts_with(lines[0], "HTTP/1.1 101") then
            return { why: "the server did not switch protocols: " + lines[0], protocol: "" }
        end if
        accept = ""
        upgrade = ""
        connection = ""
        protocol = ""
        for i = 1 to count(lines) - 1
            colon = find(lines[i], ":")
            if not is_nothing(colon) then
                name = lower(trim(left(lines[i], colon)))
                value = trim(_from(lines[i], colon + 1))
                if name = "sec-websocket-accept" then
                    accept = value
                else if name = "upgrade" then
                    upgrade = lower(value)
                else if name = "connection" then
                    connection = lower(value)
                else if name = "sec-websocket-protocol" then
                    protocol = value
                end if
            end if
        next
        if upgrade != "websocket" or not contains(connection, "upgrade") then
            return { why: "the server's answer is not a WebSocket upgrade", protocol: "" }
        end if
        if accept != st.expect then
            return { why: "the server's Sec-WebSocket-Accept does not answer the key sent", protocol: "" }
        end if
        return { why: nothing, protocol: protocol }
    end function

    ' ---- the client ------------------------------------------------------------

    ' connect(url [, options]) -> a connection. Watched (a watcher on net.events
    ' is live), the handshake is sent at once and an `open` event says it was
    ' answered; waited, connect returns once it has been.
    function connect(url, options = nothing)
        _check_options(options, "connect", ["headers", "protocols", "timeout", "tls", "max_message", "idle"])
        u = _parse_url(url)
        timeout = _option(options, "timeout", 10)
        max_message = _option(options, "max_message", 8388608)
        opts = { host: u.host, port: u.port, timeout: timeout, buffer: max_message + 65536 }
        if u.tls then
            opts.tls = _option(options, "tls", true)
        end if
        idle = _option(options, "idle", nothing)
        if not is_nothing(idle) then
            opts.idle = idle
        end if
        c = net.connect(opts)
        key = base64_encode(random_bytes(16))
        st = _new_state("client", max_message)
        st.expect = _accept_value(key)
        crlf = chr(13) + chr(10)
        req = "GET " + u.path + " HTTP/1.1" + crlf + "Host: " + u.authority + crlf
        req = req + "Upgrade: websocket" + crlf + "Connection: Upgrade" + crlf
        req = req + "Sec-WebSocket-Key: " + key + crlf + "Sec-WebSocket-Version: 13" + crlf
        protocols = _option(options, "protocols", [])
        if count(protocols) > 0 then
            req = req + "Sec-WebSocket-Protocol: " + join(protocols, ", ") + crlf
        end if
        headers = _option(options, "headers", {})
        for each name in keys(headers)
            req = req + name + ": " + headers[name] + crlf
        end for
        c.write(req + crlf)
        net.set_state(c, "websocket", st)
        if not c.watched then
            head = c.read_until(crlf + crlf, timeout)
            answer = _check_answer(head, st)
            why = answer.why
            st.protocol = answer.protocol
            if not is_nothing(why) then
                c.abort()
                error { message: "websocket.connect: " + why, source: "websocket", reason: "handshake" }
            end if
            st.open = true
            net.set_state(c, "websocket", st)
        end if
        return c
    end function

    ' ---- the server --------------------------------------------------------------

    ' Whether a request asks to become a WebSocket.
    function is_request(req)
        h = req.headers
        if not has(h, "upgrade") or not has(h, "connection") or not has(h, "sec-websocket-key") then
            return false
        end if
        return lower(h.upgrade) = "websocket" and contains(lower(h.connection), "upgrade")
    end function

    ' accept(req [, options]) -> the response a route returns. For a WebSocket
    ' handshake it is the 101 that hands the connection to net, where it
    ' arrives as an `accepted` event and websocket.events answers `open`; for
    ' anything else, a 400 (or 426 for a version this does not speak), so
    ' `return websocket.accept(req)` is always a correct route.
    function accept(req, options = nothing)
        _check_options(options, "accept", ["protocols", "max_message", "idle"])
        if req.method != "GET" or not is_request(req) then
            return { id: req.id, status: 400, headers: { "content-type": "text/plain" },
                     body: "a WebSocket handshake was expected here" + chr(10) }
        end if
        h = req.headers
        if not has(h, "sec-websocket-version") or trim(h["sec-websocket-version"]) != "13" then
            return { id: req.id, status: 426, headers: { "sec-websocket-version": "13", "content-type": "text/plain" },
                     body: "this server speaks WebSocket version 13" + chr(10) }
        end if
        key = trim(h["sec-websocket-key"])
        if byte_count(base64_decode(key)) != 16 then
            return { id: req.id, status: 400, headers: { "content-type": "text/plain" },
                     body: "Sec-WebSocket-Key must be 16 bytes in base64" + chr(10) }
        end if
        headers = { upgrade: "websocket", connection: "Upgrade" }
        headers["sec-websocket-accept"] = _accept_value(key)
        chosen = ""
        offered = []
        if has(h, "sec-websocket-protocol") then
            for each p in split(h["sec-websocket-protocol"], ",")
                append(offered, trim(p))
            end for
        end if
        for each p in _option(options, "protocols", [])
            if chosen = "" and contains(offered, p) then
                chosen = p
            end if
        end for
        if chosen != "" then
            headers["sec-websocket-protocol"] = chosen
        end if
        carried = { websocket: true, path: req.path, query: req.query, headers: req.headers,
                    remote_ip: req.remote_ip, protocol: chosen,
                    max_message: _option(options, "max_message", 8388608) }
        r = { id: req.id, status: 101, upgrade: true, headers: headers, request: carried }
        idle = _option(options, "idle", nothing)
        if not is_nothing(idle) then
            r.idle = idle
        end if
        return r
    end function

    ' ---- events, and sending ------------------------------------------------------

    ' events(e) -> the WebSocket events one net event means, as an array of
    ' { kind, connection, ... }: open (with request on a server), message
    ' (data, binary), ping and pong (data), closed (code, reason, clean) and
    ' error (code, message). A net event about anything else answers [].
    function events(e)
        c = e.connection
        if is_nothing(c) then
            return []
        end if
        if e.kind = "accepted" and has(e, "request") then
            if type(e.request) = "record" and has(e.request, "websocket") then
                st = _new_state("server", e.request.max_message)
                st.open = true
                st.protocol = e.request.protocol
                net.set_state(c, "websocket", st)
                ' Frames the client sent with its handshake are not read here:
                ' they are net's first input, so a `data` event follows this one
                ' (measured: draining here as well changed nothing).
                return [{ kind: "open", connection: c, request: e.request, protocol: st.protocol }]
            end if
        end if
        st = _state(c)
        if is_nothing(st) then
            return []
        end if
        if e.kind = "data" then
            out = []
            if not st.open then
                crlf = chr(13) + chr(10)
                head = c.take_until(crlf + crlf)
                if is_unknown(head) then
                    return []
                end if
                answer = _check_answer(head, st)
                why = answer.why
                st.protocol = answer.protocol
                if not is_nothing(why) then
                    st.closed = true
                    net.set_state(c, "websocket", st)
                    c.abort()
                    return [{ kind: "error", connection: c, code: "handshake", message: "websocket: " + why },
                            { kind: "closed", connection: c, code: 1006, reason: why, clean: false }]
                end if
                st.open = true
                net.set_state(c, "websocket", st)
                append(out, { kind: "open", connection: c, request: nothing, protocol: st.protocol })
            end if
            for each ev in _drain_frames(c, st)
                append(out, ev)
            end for
            return out
        end if
        if e.kind = "error" then
            return [{ kind: "error", connection: c, code: e.code, message: e.message }]
        end if
        if e.kind = "closed" and not st.closed then
            st.closed = true
            net.set_state(c, "websocket", st)
            return [{ kind: "closed", connection: c, code: 1006, reason: "the connection ended without a close frame",
                      clean: false }]
        end if
        return []
    end function

    function _send(c, opcode, payload, verb)
        st = _state(c)
        if is_nothing(st) then
            error { message: "websocket." + verb + ": not a WebSocket connection", source: "websocket",
                    reason: "not_websocket" }
        end if
        if not st.open or st.sent_close or not c.open then
            error { message: "websocket." + verb + ": the WebSocket is not open", source: "websocket",
                    reason: "not_open" }
        end if
        c.write(_frame(opcode, payload, st.role = "client"))
        return nothing
    end function

    ' send(c, text): one text message. It must be UTF-8 (the peer refuses
    ' anything else), so it is checked here, where the mistake is.
    function send(c, text)
        if not valid_utf8(text) then
            error { message: "websocket.send: a text message must be UTF-8; send bytes with send_binary",
                    source: "websocket", reason: "not_utf8" }
        end if
        return _send(c, 1, text, "send")
    end function

    function send_binary(c, data)
        return _send(c, 2, data, "send_binary")
    end function

    function ping(c, data = "")
        if byte_count(data) > 125 then
            error { message: "websocket.ping: at most 125 bytes", source: "websocket", reason: "too_long" }
        end if
        return _send(c, 9, data, "ping")
    end function

    ' close(c [, code, reason]): begin the closing handshake. The peer's close
    ' frame finishes it (a `closed` event, clean); waited, close waits for it
    ' up to 5 seconds.
    function close(c, code = 1000, reason = "")
        st = _state(c)
        if is_nothing(st) or st.sent_close or not c.open then
            return nothing
        end if
        if not _valid_close_code(code) then
            error { message: "websocket.close: " + string(code) + " is not a code an endpoint may send",
                    source: "websocket", reason: "bad_code" }
        end if
        c.write(_frame(8, _close_payload(code, reason), st.role = "client"))
        st.sent_close = true
        net.set_state(c, "websocket", st)
        if not c.watched then
            on error goto done
            while c.open
                r = receive(c, 5)
                if r.kind = "closed" then
                    goto done
                end if
            end while
done:
            error.clear()
            if c.open then
                c.close()
            end if
        end if
        return nothing
    end function

    ' receive(c [, timeout]) -> the next message, waited: { kind: "message",
    ' data, binary } or { kind: "closed", code, reason, clean }. A ping is
    ' answered and skipped; a pong is skipped.
    function receive(c, timeout = 30)
        st = _state(c)
        if is_nothing(st) then
            error { message: "websocket.receive: not a WebSocket connection", source: "websocket",
                    reason: "not_websocket" }
        end if
        if c.watched then
            error { message: "websocket.receive: this connection is watched; its messages come from websocket.events",
                    source: "websocket", reason: "watched" }
        end if
        while true
            if st.closed then
                return { kind: "closed", connection: c, code: 1006, reason: "already closed", clean: false }
            end if
            two = _read_or_closed(c, 2, timeout)
            if is_nothing(two) then
                st.closed = true
                net.set_state(c, "websocket", st)
                return { kind: "closed", connection: c, code: 1006, reason: "the connection ended without a close frame",
                         clean: false }
            end if
            h = _header_size(two)
            rest = ""
            if h.header > 2 then
                rest = c.read_exactly(h.header - 2, timeout)
            end if
            f = _parse_header(two + rest)
            if f.length < 0 or f.length > st.max_message then
                return _fail(c, st, 1009, "a frame is longer than max_message (" + string(st.max_message) + " bytes)")
            end if
            body = ""
            if f.length > 0 then
                body = c.read_exactly(f.length, timeout)
            end if
            ev = _on_frame(c, st, f, _payload(f, two + rest + body))
            st = _state(c)
            if not is_nothing(ev) then
                if ev.kind = "message" or ev.kind = "closed" then
                    return ev
                end if
            end if
        end while
    end function

    ' n bytes, or nothing when the peer closed first.
    function _read_or_closed(c, n, timeout)
        on error goto gone
        return c.read_exactly(n, timeout)
gone:
        ' inside the handler bare `error` is false; the fields are error.field
        code = "failed"
        if has(error.details, "code") then
            code = error.details.code
        end if
        message = error.message
        error.clear()
        if code = "closed" then
            return nothing
        end if
        error { message: message, source: "websocket", reason: code }
    end function

end library
