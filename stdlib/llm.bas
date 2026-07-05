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
            sleep_fn: unknown
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
            return encode(b)
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
        return encode(b)
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
        pref(file)= path
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
    ' WHY WE VALIDATE BEFORE decode: gBASIC's `decode` RAISES on malformed JSON,
    ' and `on error resume next` unwinds to the top program frame — so a library
    ' function cannot catch that raise and still return cleanly to its caller
    ' (the caller's assignment gets skipped). We therefore check validity with a
    ' non-raising scanner (_json_valid) and only call decode when it will succeed.
    ' The scanner is standard-JSON strict, so at worst it is stricter than decode
    ' (a spurious retry / unknown), never looser (which would crash).

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
end library
