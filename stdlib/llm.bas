' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' llm.bas — chat-completion client for gBASIC (llm_design.md §1–§4, WP-LLM-1).
'
' An LLM call is JSON over HTTPS, nothing more, so this is pure gBASIC over the
' webclient module. Two wire formats cover the provider space (§1):
'   - "anthropic" (Messages API)          -> Anthropic
'   - "openai"    (chat completions)      -> OpenAI, vLLM, Ollama, Groq, ...
' Local inference is the openai adapter with base_url pointed at your machine, so
' llm.local() is NOT a third adapter.
'
' STATE MODEL. Like edgar.bas, a model handle is a plain, copyable record; the
' setters (offline / with_transport / with_sleep) return an updated copy. The
' module is stateless — conversation state is the caller's message list (§3).
'
' FAILURE POLICY (§4). Transport/provider failures RAISE (source llm): connection
' refused, timeout, auth rejection, and 429/5xx AFTER the retry budget. Retries
' are automatic on 429/5xx with exponential backoff (1s, 2s, 4s… capped, honoring
' Retry-After when present), budget from m.retries (default 3). Model-output
' problems (empty completion; bad JSON — the latter in WP-LLM-2) return `unknown`.
'
' TESTABILITY. The transport and the backoff sleep are INJECTABLE (with_transport
' / with_sleep) so the retry loop is unit-tested with a failing transport and a
' no-wait sleep — no network, no real waiting. llm.offline(dir) is a fixture
' transport mirroring the edgar seam: it returns {dir}/{format}_response.json.
library llm
    load webclient

    ' --- model handles (§2) --------------------------------------------------

    ' A handle carries config only. Tunable fields per §2: temperature,
    ' max_tokens, timeout, retries — plus the injection seams (offline_dir,
    ' transport, sleep_fn) and key sourcing (key, keyless, env_var).
    function _handle(fmt, base_url, model, key, keyless, env_var)
        return {
            format: fmt,
            base_url: base_url,
            model: model,
            key: key,
            keyless: keyless,
            env_var: env_var,
            temperature: 0,
            max_tokens: 1024,
            timeout: 60,
            retries: 3,
            offline_dir: unknown,
            transport: unknown,
            sleep_fn: unknown,
            tools: unknown,
            max_tool_rounds: 8
        }
    end function

    function anthropic(model, key)
        return _handle("anthropic", "https://api.anthropic.com", model, key, false, "ANTHROPIC_API_KEY")
    end function

    function openai(model, key)
        return _handle("openai", "https://api.openai.com", model, key, false, "OPENAI_API_KEY")
    end function

    ' Local inference (Ollama / vLLM): the openai wire format at your base_url,
    ' no API key.
    function local(base_url, model)
        return _handle("openai", base_url, model, "", true, "")
    end function

    ' --- config / injection setters (data in, updated copy out) --------------

    function offline(m, dir)
        m.offline_dir = dir
        return m
    end function

    function with_transport(m, fn)
        m.transport = fn
        return m
    end function

    function with_sleep(m, fn)
        m.sleep_fn = fn
        return m
    end function

    ' Attach the tool registry (an array of llm.tool definitions). The registry is
    ' the ONLY authority over what the model may invoke — a tool call naming
    ' anything not in this list is refused, never dispatched dynamically. Raises on
    ' a malformed registry or duplicate tool name (a program bug, caught at
    ' configuration time rather than mid-conversation).
    function with_tools(m, tools)
        if not is_array(tools) then
            error "llm: with_tools expects an array of llm.tool definitions"
        end if
        seen = {}
        for each t in tools
            _check_tool(t)
            dup = has(seen, t.name)
            if dup then
                error "llm: duplicate tool name '" + t.name + "'"
            end if
            seen[t.name] = true
        end for
        m.tools = tools
        return m
    end function

    ' Cap on tool-executing rounds in the automatic loop (llm.run_tools).
    function with_max_tool_rounds(m, n)
        if not is_number(n) then
            error "llm: with_max_tool_rounds expects a number"
        end if
        if n < 1 then
            error "llm: with_max_tool_rounds expects at least 1"
        end if
        m.max_tool_rounds = n
        return m
    end function

    ' --- small NA-safe accessors (missing -> unknown, never raise) -----------

    function _field(rec, key)
        if is_record(rec) then
            return rec[key]
        end if
        return unknown
    end function

    function _at(arr, i)
        if is_array(arr) and i < count(arr) then
            return arr[i]
        end if
        return unknown
    end function

    ' --- key sourcing (§2): arg, else env var, else error at first call ------

    function _resolve_key(m)
        if m.keyless then
            return ""
        end if
        if is_string(m.key) and m.key != "" then
            return m.key
        end if
        ev = env(m.env_var)
        if not is_unknown(ev) and ev != "" then
            return ev
        end if
        error "llm: no API key — pass one to llm." + m.format + "() or set " + m.env_var
    end function

    ' --- adapters: request record -> wire (§1) -------------------------------

    function _endpoint(m)
        if m.format = "anthropic" then
            return m.base_url + "/v1/messages"
        end if
        return m.base_url + "/v1/chat/completions"
    end function

    function _headers(m, key)
        h = {}
        h["content-type"] = "application/json"
        if m.format = "anthropic" then
            h["x-api-key"] = key
            h["anthropic-version"] = "2023-06-01"
        else
            if key != "" then
                h["authorization"] = "Bearer " + key
            end if
        end if
        return h
    end function

    ' Build the wire body (a JSON string). Anthropic carries `system` as a
    ' top-level field; openai carries it as a leading system message. A blank
    ' system is omitted. Field insertion order is fixed so the wire is
    ' deterministic (encode preserves it) — the adapter request golden.
    function _build_body(m, system, messages)
        if m.format = "anthropic" then
            b = {}
            b.model = m.model
            b.max_tokens = m.max_tokens
            b.temperature = m.temperature
            if is_string(system) and system != "" then
                b.system = system
            end if
            b.messages = messages
            if is_array(m.tools) then
                b.tools = _tools_wire(m, m.tools)
            end if
            return json_encode(b)
        end if
        ' openai: system is the first message
        msgs = []
        if is_string(system) and system != "" then
            append(msgs, { role: "system", content: system })
        end if
        for each mm in messages
            append(msgs, mm)
        end for
        b = {}
        b.model = m.model
        b.temperature = m.temperature
        b.max_tokens = m.max_tokens
        b.messages = msgs
        if is_array(m.tools) then
            b.tools = _tools_wire(m, m.tools)
        end if
        return json_encode(b)
    end function

    ' --- adapters: wire -> response record (§3) ------------------------------
    ' Common shape: { text, usage: {input, output}, stop_reason, model, raw }.
    ' Missing pieces degrade to `unknown` (NA policy), never raise.

    function _anthropic_text(content)
        if not is_array(content) then
            return unknown
        end if
        s = ""
        for each block in content
            if _field(block, "type") = "text" then
                s = s + _field(block, "text")
            end if
        end for
        return s
    end function

    function _extract(m, resp)
        d = decode(resp.body)
        out = {}
        out.raw = d
        out.model = _field(d, "model")
        if m.format = "anthropic" then
            out.text = _anthropic_text(_field(d, "content"))
            u = _field(d, "usage")
            out.usage = { input: _field(u, "input_tokens"), output: _field(u, "output_tokens") }
            out.stop_reason = _field(d, "stop_reason")
        else
            ch0 = _at(_field(d, "choices"), 0)
            out.text = _field(_field(ch0, "message"), "content")
            u = _field(d, "usage")
            out.usage = { input: _field(u, "prompt_tokens"), output: _field(u, "completion_tokens") }
            out.stop_reason = _field(ch0, "finish_reason")
        end if
        out.tool_calls = _extract_tool_calls(m, d)
        return out
    end function

    ' --- transport (injectable > offline fixture > real http) ----------------

    function _http_transport(m, req)
        resp = webclient.request({ method: "POST", url: req.url, headers: req.headers, body: req.body, timeout: m.timeout })
        return { status: resp.status, headers: resp.headers, body: resp.body }
    end function

    ' Fixture transport mirroring the edgar seam: one response per wire format.
    function _offline_transport(m, req)
        path = m.offline_dir + "/" + m.format + "_response.json"
        pref{file}= path
        if not exists(pref) then
            error "llm: offline fixture missing (expected " + path + ")"
        end if
        return { status: 200, headers: {}, body: join(read_lines(pref), "\n") }
    end function

    function _transport_call(m, req)
        if not is_unknown(m.transport) then
            fn = m.transport
            return fn(m, req)
        end if
        if not is_unknown(m.offline_dir) then
            return _offline_transport(m, req)
        end if
        return _http_transport(m, req)
    end function

    function _sleep_call(m, seconds)
        if not is_unknown(m.sleep_fn) then
            fn = m.sleep_fn
            fn(seconds)
        else
            sleep(seconds)
        end if
    end function

    ' --- retry / backoff (§4) ------------------------------------------------

    function _is_digits(s)
        if len(s) = 0 then
            return false
        end if
        i = 0
        while i < len(s)
            c = mid(s, i, 1)
            if c < "0" or c > "9" then
                return false
            end if
            i = i + 1
        end while
        return true
    end function

    ' Retry-After (seconds) if the server sent an integer one, else unknown.
    function _retry_after(resp)
        h = resp.headers
        if not is_record(h) then
            return unknown
        end if
        v = h["Retry-After"]
        if is_unknown(v) then
            v = h["retry-after"]
        end if
        if not is_string(v) then
            return unknown
        end if
        if not _is_digits(v) then
            return unknown
        end if
        return number(v)
    end function

    ' Backoff seconds for a given attempt: Retry-After if present, else
    ' exponential 1,2,4,… capped at 30.
    function _backoff_delay(attempt, resp)
        ra = _retry_after(resp)
        if not is_unknown(ra) then
            return ra
        end if
        d = 1
        i = 0
        while i < attempt
            d = d * 2
            i = i + 1
        end while
        if d > 30 then
            d = 30
        end if
        return d
    end function

    ' Perform the request with the retry budget. 200 -> the response; 429/5xx ->
    ' backoff + retry while attempts remain; anything else, or budget exhausted,
    ' raises (§4). m.retries backoffs, then raise.
    function _send(m, endpoint, headers, body)
        attempt = 0
        result = unknown
        done = false
        while not done
            req = { url: endpoint, headers: headers, body: body, attempt: attempt }
            resp = _transport_call(m, req)
            if resp.status = 200 then
                result = resp
                done = true
            else
                retryable = resp.status = 429 or resp.status >= 500
                if retryable and attempt < m.retries then
                    _sleep_call(m, _backoff_delay(attempt, resp))
                    attempt = attempt + 1
                else
                    error "llm: request failed (HTTP " + string(resp.status) + ") after " + string(attempt) + " retries"
                end if
            end if
        end while
        return result
    end function

    ' --- calls (§3) ----------------------------------------------------------

    ' Full form: system prompt + explicit message list. Returns the common
    ' response record { text, usage: {input, output}, stop_reason, model, raw }.
    function chat(m, system, messages)
        key = _resolve_key(m)
        endpoint = _endpoint(m)
        headers = _headers(m, key)
        body = _build_body(m, system, messages)
        resp = _send(m, endpoint, headers, body)
        return _extract(m, resp)
    end function

    ' The 90% case: one user turn -> the assistant's text (a string, or unknown
    ' on an empty completion).
    function ask(m, system, prompt)
        r = chat(m, system, [ { role: "user", content: prompt } ])
        return r.text
    end function

    ' --- structured output: ask_json (§3, §7.1) ------------------------------
    '
    ' llm.ask_json(m, system, prompt) -> record/list, or unknown.
    '
    ' Owns the sloppiness handling: strip markdown fences, then decode. On a decode
    ' failure, issue exactly ONE corrective retry ("your previous reply was not
    ' valid JSON; reply with only the JSON"), then return `unknown` (§3). That
    ' corrective retry is its OWN single-shot budget — it does NOT consume
    ' m.retries (which is the transport-level 429/5xx budget); §7.1 open question,
    ' resolved to "separate, always exactly one".
    '
    ' WHY WE VALIDATE BEFORE decode: `decode` RAISES on malformed JSON, and when
    ' this was written a library function could not catch that raise and still
    ' return cleanly to its caller — `on error resume next` was process-global
    ' and abandoned the caller's assignment regardless. PLAT-ERR (2026-08-23)
    ' ended that: `on error goto next` is frame-scoped and catch-and-return works,
    ' so this preflight is now a CHOICE rather than a workaround. It stays for
    ' two reasons of its own: the scanner reports WHERE the payload is malformed,
    ' which a caught raise would only describe, and a provider returning junk is
    ' an expected condition on this path rather than an exceptional one. The
    ' scanner is standard-JSON strict, so at worst it is stricter than decode (a
    ' spurious retry / unknown), never looser (which would crash).

    function _ch(s, i, n)
        if i >= n then
            return ""
        end if
        return mid(s, i, 1)
    end function

    function _is_digit(c)
        return c >= "0" and c <= "9"
    end function

    function _is_hex(c)
        if c >= "0" and c <= "9" then
            return true
        end if
        if c >= "a" and c <= "f" then
            return true
        end if
        if c >= "A" and c <= "F" then
            return true
        end if
        return false
    end function

    function _skip_ws(s, i, n)
        while i < n
            c = mid(s, i, 1)
            if c = " " or c = chr(9) or c = chr(10) or c = chr(13) then
                i = i + 1
            else
                return i
            end if
        end while
        return i
    end function

    ' i points at the opening quote; return index just past the closing quote, or
    ' -1. Escapes are validated to the JSON set so we never accept what decode
    ' would reject.
    function _scan_string(s, i, n)
        q = chr(34)
        bs = chr(92)
        i = i + 1
        while i < n
            c = mid(s, i, 1)
            if c = q then
                return i + 1
            end if
            if c = bs then
                e = _ch(s, i + 1, n)
                if e = q or e = bs or e = "/" or e = "b" or e = "f" or e = "n" or e = "r" or e = "t" then
                    i = i + 2
                else
                    if e = "u" then
                        ok = true
                        j = 0
                        while j < 4
                            if not _is_hex(_ch(s, i + 2 + j, n)) then
                                ok = false
                            end if
                            j = j + 1
                        end while
                        if not ok then
                            return -1
                        end if
                        i = i + 6
                    else
                        return -1
                    end if
                end if
            else
                i = i + 1
            end if
        end while
        return -1
    end function

    function _scan_number(s, i, n)
        start = i
        if _ch(s, i, n) = "-" then
            i = i + 1
        end if
        c = _ch(s, i, n)
        if c = "0" then
            i = i + 1
        else
            if _is_digit(c) then
                while _is_digit(_ch(s, i, n))
                    i = i + 1
                end while
            else
                return -1
            end if
        end if
        if _ch(s, i, n) = "." then
            i = i + 1
            if not _is_digit(_ch(s, i, n)) then
                return -1
            end if
            while _is_digit(_ch(s, i, n))
                i = i + 1
            end while
        end if
        c = _ch(s, i, n)
        if c = "e" or c = "E" then
            i = i + 1
            c = _ch(s, i, n)
            if c = "+" or c = "-" then
                i = i + 1
            end if
            if not _is_digit(_ch(s, i, n)) then
                return -1
            end if
            while _is_digit(_ch(s, i, n))
                i = i + 1
            end while
        end if
        if i = start then
            return -1
        end if
        return i
    end function

    function _scan_lit(s, i, n, word)
        w = len(word)
        if i + w > n then
            return -1
        end if
        if mid(s, i, w) = word then
            return i + w
        end if
        return -1
    end function

    function _scan_array(s, i, n)
        i = i + 1
        i = _skip_ws(s, i, n)
        if _ch(s, i, n) = "]" then
            return i + 1
        end if
        while true
            i = _scan_value(s, i, n)
            if i < 0 then
                return -1
            end if
            i = _skip_ws(s, i, n)
            c = _ch(s, i, n)
            if c = "," then
                i = i + 1
            else
                if c = "]" then
                    return i + 1
                end if
                return -1
            end if
        end while
    end function

    function _scan_object(s, i, n)
        q = chr(34)
        i = i + 1
        i = _skip_ws(s, i, n)
        if _ch(s, i, n) = "}" then
            return i + 1
        end if
        while true
            i = _skip_ws(s, i, n)
            if _ch(s, i, n) != q then
                return -1
            end if
            i = _scan_string(s, i, n)
            if i < 0 then
                return -1
            end if
            i = _skip_ws(s, i, n)
            if _ch(s, i, n) != ":" then
                return -1
            end if
            i = i + 1
            i = _scan_value(s, i, n)
            if i < 0 then
                return -1
            end if
            i = _skip_ws(s, i, n)
            c = _ch(s, i, n)
            if c = "," then
                i = i + 1
            else
                if c = "}" then
                    return i + 1
                end if
                return -1
            end if
        end while
    end function

    function _scan_value(s, i, n)
        i = _skip_ws(s, i, n)
        c = _ch(s, i, n)
        if c = "" then
            return -1
        end if
        if c = chr(34) then
            return _scan_string(s, i, n)
        end if
        if c = "{" then
            return _scan_object(s, i, n)
        end if
        if c = "[" then
            return _scan_array(s, i, n)
        end if
        if c = "-" or _is_digit(c) then
            return _scan_number(s, i, n)
        end if
        if c = "t" then
            return _scan_lit(s, i, n, "true")
        end if
        if c = "f" then
            return _scan_lit(s, i, n, "false")
        end if
        if c = "n" then
            return _scan_lit(s, i, n, "null")
        end if
        return -1
    end function

    ' True iff `s` is exactly one well-formed JSON value (trailing whitespace ok).
    ' Non-raising — safe to call before decode.
    function _json_valid(s)
        n = len(s)
        i = _scan_value(s, 0, n)
        if i < 0 then
            return false
        end if
        i = _skip_ws(s, i, n)
        return i = n
    end function

    ' Strip a leading ```/```json fence and its closing ``` (a common local-model
    ' sloppiness), returning the trimmed inner text. Leaves unfenced text as-is.
    function _strip_fences(text)
        s = trim(text)
        if find(s, "```") = 0 then
            nl = find(s, chr(10))
            if nl != nothing then
                s = mid(s, nl + 1, len(s) - nl - 1)
            end if
            c = find(s, "```")
            if c != nothing then
                s = left(s, c)
            end if
        end if
        return trim(s)
    end function

    ' Fence-strip + validate + decode; unknown on anything that isn't clean JSON.
    ' decode is only reached on validated input, so it never raises.
    function _parse_or_unknown(text)
        if not is_string(text) then
            return unknown
        end if
        s = _strip_fences(text)
        if _json_valid(s) then
            return decode(s)
        end if
        return unknown
    end function

    function ask_json(m, system, prompt)
        r = chat(m, system, [ { role: "user", content: prompt } ])
        parsed = _parse_or_unknown(r.text)
        if not is_unknown(parsed) then
            return parsed
        end if
        ' one corrective retry — its own single-shot budget (§7.1)
        messages = [
            { role: "user", content: prompt },
            { role: "assistant", content: r.text },
            { role: "user", content: "Your previous reply was not valid JSON. Reply with ONLY the JSON value — no prose, no markdown fences." }
        ]
        r2 = chat(m, system, messages)
        return _parse_or_unknown(r2.text)
    end function

    ' =======================================================================
    ' TOOL / FUNCTION CALLING (NAP-13)
    ' =======================================================================
    '
    ' A tool is a gBASIC function the model may ask you to run. The library
    ' normalizes both wire formats to ONE internal shape, so programs never see
    ' provider field names:
    '
    '   tool definition : { name, description, schema, fn }
    '   tool call       : { id, name, arguments, error }
    '   tool result     : { id, name, content, is_error }
    '
    ' SECURITY. The registry passed to with_tools is the sole authority over what
    ' is callable. A tool call is dispatched only if its name matches a registered
    ' definition; an unknown name yields a controlled error result. Model-supplied
    ' arguments are parsed as JSON into records and passed as VALUES — they are
    ' never evaluated as gBASIC source, and no dynamic name lookup is performed.
    '
    ' CALLABLE SIGNATURE. A tool function takes exactly ONE argument: a record of
    ' the decoded arguments, e.g. `function get_customer(args)` reading `args.id`.
    ' One stable rule, no positional translation.
    '
    ' TOOLS MUST BE TOTAL — they must RETURN, never `error`. This was once a
    ' hard constraint (a raising tool aborted the whole program, because a library
    ' could not catch a raise from a function it called); PLAT-ERR made it an API
    ' RULE instead, and the loop could now contain a raising tool with
    ' `on error goto next` around the dispatch. Keeping the rule is deliberate: a
    ' tool result is data the MODEL reads, so a failure has to be describable in
    ' that data, and a caught raise would have to be converted into exactly this
    ' shape anyway. To report failure, RETURN a failure record built
    ' with `llm.tool_error("no such customer")` (a constructor because `error` is a
    ' reserved word, so `{ error: ... }` will not parse). That becomes a
    ' controlled is_error tool result the model can react to. Everything the
    ' library can check itself (arguments, required fields, result serializability)
    ' is PRE-VALIDATED before the call, matching the _json_valid-before-decode rule
    ' used by ask_json.

    ' ---- tool definitions --------------------------------------------------

    function _valid_tool_name(name)
        n = len(name)
        if n = 0 or n > 64 then
            return false
        end if
        i = 0
        while i < n
            c = mid(name, i, 1)
            ok = false
            if c >= "a" and c <= "z" then ok = true
            if c >= "A" and c <= "Z" then ok = true
            if c >= "0" and c <= "9" then ok = true
            if c = "_" or c = "-" then ok = true
            if not ok then
                return false
            end if
            i = i + 1
        end while
        return true
    end function

    ' Raise unless `t` is a well-formed tool definition (a program bug — surfaced
    ' at configuration time, not mid-conversation).
    function _check_tool(t)
        if not is_record(t) then
            error "llm: tool definition must be a record (use llm.tool)"
        end if
        nameok = false
        if is_string(t.name) then
            nameok = _valid_tool_name(t.name)
        end if
        if not nameok then
            error "llm: tool name must be 1-64 chars of letters, digits, '_' or '-'"
        end if
        if not is_string(t.description) then
            error "llm: tool '" + t.name + "' needs a string description"
        end if
        if not is_record(t.schema) then
            error "llm: tool '" + t.name + "' needs a record parameter schema"
        end if
        k = reflect.kind(t.fn)
        if k != "function" then
            error "llm: tool '" + t.name + "' needs a callable function value"
        end if
        return nothing
    end function

    ' How a tool reports failure: `return llm.tool_error("no such customer")`.
    ' A constructor is provided because `error` is a reserved word in gBASIC, so
    ' the natural literal `{ error: "..." }` will not parse — the key has to be set
    ' dynamically, which this hides.
    function tool_error(message)
        r = {}
        r["error"] = message
        return r
    end function

    ' Build a tool definition. `schema` is a JSON-Schema-subset record, e.g.
    '   { type: "object", properties: { id: { type: "number" } }, required: ["id"] }
    function tool(name, description, schema, fn)
        t = { name: name, description: description, schema: schema, fn: fn }
        _check_tool(t)
        return t
    end function

    ' ---- adapters: tools -> wire ------------------------------------------

    function _tools_wire(m, tools)
        out = []
        for each t in tools
            if m.format = "anthropic" then
                w = {}
                w["name"] = t.name
                w["description"] = t.description
                w["input_schema"] = t.schema
                append(out, w)
            else
                f = {}
                f["name"] = t.name
                f["description"] = t.description
                f["parameters"] = t.schema
                w = {}
                w["type"] = "function"
                w["function"] = f
                append(out, w)
            end if
        end for
        return out
    end function

    ' ---- adapters: wire -> normalized tool calls ---------------------------

    ' A provider argument payload -> a record, or a normalized error string.
    ' OpenAI sends arguments as a JSON *string*; Anthropic sends an object.
    function _call_args(raw)
        if is_record(raw) then
            return { ok: true, value: raw }
        end if
        if is_string(raw) then
            s = trim(raw)
            if s = "" then
                return { ok: true, value: {} }
            end if
            if not _json_valid(s) then
                return { ok: false, message: "malformed tool arguments (not valid JSON)" }
            end if
            v = decode(s)
            if not is_record(v) then
                return { ok: false, message: "tool arguments must be a JSON object" }
            end if
            return { ok: true, value: v }
        end if
        if is_unknown(raw) or is_nothing(raw) then
            return { ok: true, value: {} }
        end if
        return { ok: false, message: "tool arguments must be a JSON object" }
    end function

    function _norm_call(id, name, raw_args)
        p = _call_args(raw_args)
        c = {}
        c["id"] = id
        c["name"] = name
        if p.ok then
            c["arguments"] = p.value
            c["error"] = unknown
        else
            c["arguments"] = {}
            c["error"] = p.message
        end if
        return c
    end function

    ' Normalized tool calls from a provider response body. Order and ids are
    ' preserved exactly as the provider sent them.
    function _extract_tool_calls(m, d)
        calls = []
        if m.format = "anthropic" then
            content = _field(d, "content")
            if is_array(content) then
                for each block in content
                    bt = _field(block, "type")
                    if bt = "tool_use" then
                        append(calls, _norm_call(_field(block, "id"), _field(block, "name"), _field(block, "input")))
                    end if
                end for
            end if
            return calls
        end if
        ch0 = _at(_field(d, "choices"), 0)
        tcs = _field(_field(ch0, "message"), "tool_calls")
        if is_array(tcs) then
            for each tc in tcs
                fn = _field(tc, "function")
                append(calls, _norm_call(_field(tc, "id"), _field(fn, "name"), _field(fn, "arguments")))
            end for
        end if
        return calls
    end function

    ' Public: the normalized tool calls carried by a chat() response (possibly []).
    function tool_calls(response)
        tc = _field(response, "tool_calls")
        if is_array(tc) then
            return tc
        end if
        return []
    end function

    ' ---- argument validation (JSON-Schema subset) --------------------------

    ' Supported: object/properties/required, and property types string, number,
    ' integer, boolean, array, object. Anything else is accepted unchecked — this
    ' is a practical guard against obviously wrong model output, NOT a complete
    ' JSON Schema validator (the provider does its own validation too).
    function _type_ok(v, want)
        k = reflect.kind(v)
        if want = "string" then return k = "string"
        if want = "boolean" then return k = "boolean"
        if want = "array" then return k = "array"
        if want = "object" then return k = "record"
        if want = "number" then return k = "number"
        if want = "integer" then
            if k != "number" then
                return false
            end if
            r = v - floor(v)
            return r = 0
        end if
        return true
    end function

    ' "" when the arguments satisfy the schema, else a human-readable reason.
    function _validate_args(schema, args)
        req = _field(schema, "required")
        if is_array(req) then
            for each rname in req
                if is_string(rname) then
                    present = has(args, rname)
                    if not present then
                        return "missing required field '" + rname + "'"
                    end if
                end if
            end for
        end if
        props = _field(schema, "properties")
        if is_record(props) then
            for each pname in keys(props)
                present = has(args, pname)
                if present then
                    want = _field(_field(props, pname), "type")
                    if is_string(want) then
                        good = _type_ok(args[pname], want)
                        if not good then
                            return "field '" + pname + "' should be " + want
                        end if
                    end if
                end if
            end for
        end if
        return ""
    end function

    ' ---- result serialization ---------------------------------------------

    ' True iff `v` contains only values encode() accepts. Pre-checked rather
    ' than caught: a tool result carrying a function or a handle is a PROGRAMMING
    ' error in the tool, and naming it beats turning it into a caught raise the
    ' model would then read as data. (A library can catch a raise since
    ' PLAT-ERR; this stays a check on purpose.)
    function _serializable(v)
        k = reflect.kind(v)
        if k = "number" or k = "string" or k = "boolean" then return true
        if k = "nothing" or k = "unknown" then return true
        if k = "array" then
            for each e in v
                ok = _serializable(e)
                if not ok then
                    return false
                end if
            end for
            return true
        end if
        if k = "record" then
            for each key in keys(v)
                ok = _serializable(v[key])
                if not ok then
                    return false
                end if
            end for
            return true
        end if
        return false
    end function

    ' Tool return value -> the string carried back to the model. Strings pass
    ' through verbatim (text results shouldn't gain JSON quotes); everything else
    ' is JSON-encoded.
    '
    ' The whole wire path uses `json_encode` (strict RFC 8259), never `encode`
    ' (gBASIC's dialect, which spells the empty values `nothing`/`unknown` and is
    ' not valid JSON). `nothing` therefore serializes as `null` on its own, so no
    ' sanitizing is needed for it — see _drop_unknown for the one value JSON
    ' genuinely cannot express.
    function _result_content(v)
        if is_string(v) then
            return v
        end if
        ok = json_encodable(v)
        if not ok then
            return "null"
        end if
        return json_encode(v)
    end function

    ' Remove `unknown` — gBASIC's NA — from a value bound for the wire. It is the
    ' ONE thing strict JSON cannot represent (json_encode refuses it, correctly:
    ' `nothing`/null means "no value", NA means "value not known"). It reaches here
    ' only from _field on a key the provider omitted, so dropping the field is the
    ' faithful reading. `nothing` is deliberately left alone — it encodes as null,
    ' which is exactly what the openai assistant tool-call turn carries.
    '
    ' PREFLIGHT, not catch: json_encode RAISES on a value it cannot represent.
    ' Containing that is possible since PLAT-ERR (docs/error_model_design.md), but
    ' dropping the unknown field is not error handling — it is the faithful
    ' reading of an absent value, and it is what keeps the payload valid rather
    ' than merely reporting that it is not.
    function _drop_unknown(v)
        k = reflect.kind(v)
        if k = "record" then
            out = {}
            for each key in keys(v)
                e = v[key]
                ek = reflect.kind(e)
                if ek != "unknown" then
                    out[key] = _drop_unknown(e)
                end if
            end for
            return out
        end if
        if k = "array" then
            out = []
            for each e in v
                append(out, _drop_unknown(e))
            end for
            return out
        end if
        return v
    end function

    ' ---- dispatch ----------------------------------------------------------

    function _find_tool(tools, name)
        if is_array(tools) then
            for each t in tools
                if t.name = name then
                    return t
                end if
            end for
        end if
        return unknown
    end function

    function _result(id, name, content, is_error)
        r = {}
        r["id"] = id
        r["name"] = name
        r["content"] = content
        r["is_error"] = is_error
        return r
    end function

    ' Execute one normalized call against the registry. Every failure mode that
    ' the library can detect becomes a controlled is_error RESULT (which the model
    ' can read and react to) rather than a raise.
    function _execute_one(m, c)
        cerr = c["error"]
        if is_string(cerr) then
            return _result(c.id, c.name, cerr, true)
        end if
        t = _find_tool(m.tools, c.name)
        if is_unknown(t) then
            return _result(c.id, c.name, "unknown tool '" + string(c.name) + "'", true)
        end if
        why = _validate_args(t.schema, c.arguments)
        if why != "" then
            return _result(c.id, c.name, "invalid arguments: " + why, true)
        end if
        fn = t.fn
        v = fn(c.arguments)
        ' A tool reports failure by RETURNING { error: "..." } (it must not raise).
        if is_record(v) then
            e = _field(v, "error")
            if is_string(e) then
                return _result(c.id, c.name, e, true)
            end if
        end if
        ok = _serializable(v)
        if not ok then
            return _result(c.id, c.name, "tool result is not serializable", true)
        end if
        return _result(c.id, c.name, _result_content(v), false)
    end function

    ' Public: run every normalized call in order, returning normalized results.
    function execute_tools(m, calls)
        out = []
        for each c in calls
            append(out, _execute_one(m, c))
        end for
        return out
    end function

    ' ---- continuation messages --------------------------------------------

    ' The assistant turn to replay, in the provider's own shape (it must be echoed
    ' back verbatim so tool-call ids line up).
    function _assistant_message(m, response)
        d = response.raw
        if m.format = "anthropic" then
            msg = {}
            msg["role"] = "assistant"
            msg["content"] = _drop_unknown(_field(d, "content"))
            return msg
        end if
        return _drop_unknown(_field(_at(_field(d, "choices"), 0), "message"))
    end function

    ' Provider-shaped tool-result messages for one assistant turn.
    function _tool_result_messages(m, results)
        out = []
        if m.format = "anthropic" then
            blocks = []
            for each r in results
                b = {}
                b["type"] = "tool_result"
                b["tool_use_id"] = r.id
                b["content"] = r.content
                if r.is_error then
                    b["is_error"] = true
                end if
                append(blocks, b)
            end for
            msg = {}
            msg["role"] = "user"
            msg["content"] = blocks
            append(out, msg)
            return out
        end if
        for each r in results
            msg = {}
            msg["role"] = "tool"
            msg["tool_call_id"] = r.id
            msg["content"] = r.content
            append(out, msg)
        end for
        return out
    end function

    ' Public: messages + this assistant turn + its tool results -> the message list
    ' for the next request. Use with chat/tool_calls/execute_tools to drive the
    ' loop manually; run_tools does it for you.
    function append_tool_results(m, messages, response, results)
        out = []
        for each mm in messages
            append(out, mm)
        end for
        append(out, _assistant_message(m, response))
        for each tm in _tool_result_messages(m, results)
            append(out, tm)
        end for
        return out
    end function

    ' ---- the automatic loop ------------------------------------------------

    ' Send, run any requested tools, send again, until the model answers without
    ' calling tools. Returns the final response record with two extra fields:
    '   rounds   — tool-executing rounds performed
    '   messages — the full transcript including tool turns
    ' Raises if the model keeps calling tools past m.max_tool_rounds.
    function run_tools(m, system, messages)
        msgs = []
        for each mm in messages
            append(msgs, mm)
        end for
        rounds = 0
        while true
            r = chat(m, system, msgs)
            calls = tool_calls(r)
            n = count(calls)
            if n = 0 then
                r.rounds = rounds
                r.messages = msgs
                return r
            end if
            if rounds >= m.max_tool_rounds then
                error "llm: tool loop exceeded " + string(m.max_tool_rounds) + " rounds"
            end if
            results = execute_tools(m, calls)
            msgs = append_tool_results(m, msgs, r, results)
            rounds = rounds + 1
        end while
    end function
end library
