' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.

' crypto.bas — ergonomic cryptography compositions in gBASIC.
'
' Built entirely on the crypto C builtins (docs/crypto_design.md Phases 1-2-4):
' base64url/hex encoders, hmac_sha256, sha256/sha512, bytes_equal, random_bytes,
' and aes_gcm_encrypt/decrypt. This is the Phase 3 layer — the safe-to-compose
' conveniences (hex digests, signed cookies, CSRF tokens, JWT/HS256, and
' nonce-managing symmetric encryption) that web apps reach for.
'
' A tiny JSON DECODER for FLAT objects (string / number / bool / null values)
' lives here so JWT needs no new C dependency. It is not a general JSON library:
' nested objects and arrays are out of scope, and `jwt_encode` refuses a nested
' payload rather than minting a token this decoder cannot read back. The matching
' encoder was retired in 0.1.0-rc8 in favour of the core `json_encode` builtin --
' see the note above the decoder for why the decoder did not go with it.
'
' Out-of-domain inputs return `unknown` (bad signature, malformed token,
' tampered ciphertext), never a bogus value.
library crypto

    ' ---- hex / token convenience -------------------------------------------

    function sha256_hex(s)
        return hex_encode(sha256(s))
    end function

    function sha512_hex(s)
        return hex_encode(sha512(s))
    end function

    ' n random bytes as lowercase hex (2n characters).
    function random_hex(n)
        return hex_encode(random_bytes(n))
    end function

    ' n random bytes as a URL-safe base64 token (no padding).
    function random_token(n)
        return base64url_encode(random_bytes(n))
    end function

    ' ---- signed cookies (HMAC-SHA256, tamper-evident) ----------------------
    ' sign_cookie returns "<b64url(value)>.<b64url(hmac)>"; verify_cookie checks
    ' the MAC in constant time and returns the original value, or `unknown`.

    function sign_cookie(value, secret)
        encoded = base64url_encode(value)
        sig = base64url_encode(hmac_sha256(secret, encoded))
        return encoded + "." + sig
    end function

    function verify_cookie(signed, secret)
        parts = split(signed, ".")
        if len(parts) != 2 then
            return unknown
        end if
        expected = base64url_encode(hmac_sha256(secret, parts[0]))
        if not bytes_equal(expected, parts[1]) then
            return unknown
        end if
        value = base64url_decode(parts[0])
        if is_unknown(value) then
            return unknown
        end if
        return value
    end function

    ' ---- CSRF tokens (double-submit, HMAC over the session id) -------------

    function csrf_token(secret, session)
        return base64url_encode(hmac_sha256(secret, session))
    end function

    function csrf_check(token, secret, session)
        return bytes_equal(token, csrf_token(secret, session))
    end function

    ' ---- symmetric encryption (AES-GCM, random nonce packed in) ------------
    ' encrypt prepends a fresh 12-byte nonce: blob = nonce || ciphertext || tag.
    ' decrypt splits it back out and verifies the tag (returns `unknown` on any
    ' tampering or wrong key). key must be 16, 24, or 32 bytes.

    function _byte_slice(s, start, count)
        out = []
        i = 0
        while i < count
            append(out, byte_at(s, start + i))
            i = i + 1
        end while
        return from_bytes(out)
    end function

    function encrypt(key, plaintext)
        nonce = random_bytes(12)
        body = aes_gcm_encrypt(key, nonce, plaintext, "")
        if is_unknown(body) then
            return unknown
        end if
        return nonce + body
    end function

    function decrypt(key, blob)
        n = byte_count(blob)
        if n < 12 then
            return unknown
        end if
        nonce = _byte_slice(blob, 0, 12)
        body = _byte_slice(blob, 12, n - 12)
        return aes_gcm_decrypt(key, nonce, body, "")
    end function

    ' ---- flat-JSON DECODING, and why only decoding ------------------------
    '
    ' The encoder that lived here is gone (0.1.0-rc8): the core `json_encode`
    ' builtin does the same job at any depth and to RFC 8259, and since it became
    ' a builtin this library's version was not even reachable by the natural call
    ' -- `json_encode(x)` resolved to the builtin, and the runtime warned on every
    ' `load crypto` that it was doing so. Only `crypto.json_encode(x)`, spelled
    ' qualified, reached the flat one, which then refused anything nested.
    '
    ' The DECODER stays, and is not the same question. It reads a JWT payload,
    ' which is attacker-supplied: it accepts RFC 8259 and nothing else. The core's
    ' non-raising decoder is `try_decode`, and that one deliberately speaks the
    ' gBASIC DIALECT -- bare `nothing`, `unknown`, `inf`, `nan` -- so swapping it
    ' in would let a crafted token carry values JSON has no syntax for. There is
    ' no strict non-raising full-depth decoder in the core to fold into; until
    ' there is, this one earns its place by being strict.

    function _json_is_ws(ch)
        return ch = " " or ch = "\n" or ch = "\t" or ch = "\u{000d}"
    end function

    function _json_skip_ws(s, i)
        while i < len(s)
            if _json_is_ws(mid(s, i, 1)) then
                i = i + 1
            else
                return i
            end if
        end while
        return i
    end function

    function _json_parse_string(s, i)
        ' s[i] is the opening quote
        i = i + 1
        out = ""
        while i < len(s)
            ch = mid(s, i, 1)
            if ch = "\"" then
                return { ok: true, value: out, pos: i + 1 }
            end if
            if ch = "\\" then
                i = i + 1
                if i >= len(s) then
                    return { ok: false, value: "", pos: i }
                end if
                esc = mid(s, i, 1)
                if esc = "n" then
                    out = out + "\n"
                else
                    if esc = "t" then
                        out = out + "\t"
                    else
                        if esc = "r" then
                            out = out + "\u{000d}"
                        else
                            out = out + esc
                        end if
                    end if
                end if
            else
                out = out + ch
            end if
            i = i + 1
        end while
        return { ok: false, value: "", pos: i }
    end function

    function _json_is_digit(ch)
        c = code(ch)
        return c >= code("0") and c <= code("9")
    end function

    function _json_digit_run(s, i)
        n = 0
        while i + n < len(s)
            if _json_is_digit(mid(s, i + n, 1)) then
                n = n + 1
            else
                return n
            end if
        end while
        return n
    end function

    ' RFC 8259 number, SCANNED AGAINST THE GRAMMAR:
    '     -? ( 0 | [1-9][0-9]* ) ( . [0-9]+ )? ( [eE] [+-]? [0-9]+ )?
    '
    ' Not "collect the number-ish characters and convert them". `number()`
    ' RAISES on a string it cannot read, gBASIC cannot catch a raise, and this
    ' parser reads ATTACKER-SUPPLIED JWT payloads -- so a span it cannot convert
    ' has to be a refusal and can never be a raise. It was one: _json_parse_value
    ' falls through to a number for every character that is not `"`, `t`, `f` or
    ' `n`, so `{"a":inf}` reached number("") and killed the program instead of
    ' being rejected as the malformed token it is. The door was as wide as the
    ' alphabet.
    '
    ' Scanning the real grammar also tightens what is accepted: the old
    ' character-class test admitted a leading `+`, a bare `-`, `1.2.3` and `1e`,
    ' none of which are JSON, and every one of which raised.
    function _json_parse_number(s, i)
        bad = { ok: false, value: unknown, pos: i }
        j = i
        if j < len(s) then
            if mid(s, j, 1) = "-" then
                j = j + 1
            end if
        end if
        if j >= len(s) then
            return bad
        end if
        c = mid(s, j, 1)
        if c = "0" then
            j = j + 1
            ' A leading zero may not be followed by a digit (`01` is not JSON).
            if j < len(s) then
                if _json_is_digit(mid(s, j, 1)) then
                    return bad
                end if
            end if
        else
            if not _json_is_digit(c) then
                return bad
            end if
            j = j + _json_digit_run(s, j)
        end if
        if j < len(s) then
            if mid(s, j, 1) = "." then
                j = j + 1
                d = _json_digit_run(s, j)
                if d = 0 then
                    return bad
                end if
                j = j + d
            end if
        end if
        if j < len(s) then
            e = mid(s, j, 1)
            if e = "e" or e = "E" then
                j = j + 1
                if j < len(s) then
                    sgn = mid(s, j, 1)
                    if sgn = "+" or sgn = "-" then
                        j = j + 1
                    end if
                end if
                d = _json_digit_run(s, j)
                if d = 0 then
                    return bad
                end if
                j = j + d
            end if
        end if
        v = number(mid(s, i, j - i))
        ' A magnitude no double can hold (`1e400`) converts to infinity without
        ' raising, which would smuggle a non-finite value into a token payload
        ' through syntax RFC 8259 permits. `v - v` is 0 for every finite value
        ' and NaN for infinity and NaN alike, so one test covers both -- and
        ' gBASIC has no isfinite to ask instead.
        if (v - v) != 0 then
            return bad
        end if
        return { ok: true, value: v, pos: j }
    end function

    function _json_parse_value(s, i)
        i = _json_skip_ws(s, i)
        if i >= len(s) then
            return { ok: false, value: unknown, pos: i }
        end if
        ch = mid(s, i, 1)
        if ch = "\"" then
            return _json_parse_string(s, i)
        end if
        if ch = "t" then
            if mid(s, i, 4) = "true" then
                return { ok: true, value: true, pos: i + 4 }
            end if
            return { ok: false, value: unknown, pos: i }
        end if
        if ch = "f" then
            if mid(s, i, 5) = "false" then
                return { ok: true, value: false, pos: i + 5 }
            end if
            return { ok: false, value: unknown, pos: i }
        end if
        if ch = "n" then
            if mid(s, i, 4) = "null" then
                return { ok: true, value: unknown, pos: i + 4 }
            end if
            return { ok: false, value: unknown, pos: i }
        end if
        return _json_parse_number(s, i)
    end function

    function json_decode(s)
        if not is_string(s) then
            return unknown
        end if
        i = _json_skip_ws(s, 0)
        if i >= len(s) then
            return unknown
        end if
        if mid(s, i, 1) != "{" then
            return unknown
        end if
        i = i + 1
        out = {}
        i = _json_skip_ws(s, i)
        if i < len(s) then
            if mid(s, i, 1) = "}" then
                return out
            end if
        end if
        while i < len(s)
            i = _json_skip_ws(s, i)
            if mid(s, i, 1) != "\"" then
                return unknown
            end if
            ks = _json_parse_string(s, i)
            if not ks.ok then
                return unknown
            end if
            i = _json_skip_ws(s, ks.pos)
            if i >= len(s) then
                return unknown
            end if
            if mid(s, i, 1) != ":" then
                return unknown
            end if
            vs = _json_parse_value(s, i + 1)
            if not vs.ok then
                return unknown
            end if
            out[ks.value] = vs.value
            i = _json_skip_ws(s, vs.pos)
            if i >= len(s) then
                return unknown
            end if
            ch = mid(s, i, 1)
            if ch = "," then
                i = i + 1
            else
                if ch = "}" then
                    return out
                else
                    return unknown
                end if
            end if
        end while
        return unknown
    end function

    ' ---- JWT (HS256) -------------------------------------------------------
    ' jwt_encode signs a flat payload record; jwt_verify checks the signature
    ' (constant time), enforces the `exp` claim (Unix seconds) against epoch()
    ' when present, and returns the decoded payload record, or `unknown` on a bad
    ' signature / malformed token / expired token. The header is fixed:
    ' {"alg":"HS256","typ":"JWT"}.

    ' A payload every field of which this library's DECODER can read back.
    '
    ' The encoder is now the core builtin, which handles any depth; the decoder
    ' is still flat by design. Widening one side would let jwt_encode mint a
    ' token jwt_verify answers `unknown` for -- the same round-trip hole rc7
    ' closed between `encode` and `decode`, reintroduced one layer up. The pair
    ' is kept honest here, at the encoder, because refusing to sign is the safe
    ' direction.
    function _jwt_flat(payload)
        for each k in keys(payload)
            kind = reflect.kind(payload[k])
            if kind = "record" or kind = "array" then
                return false
            end if
        end for
        return true
    end function

    function jwt_encode(payload, secret)
        header = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}"
        ' PREFLIGHT, not catch: the core `json_encode` RAISES on a value JSON
        ' cannot represent, and this function's contract is to answer `unknown`
        ' rather than to end the program. `json_encodable` is the side-effect-free
        ' twin that exists for exactly this position (the same pattern
        ' stdlib/llm.bas uses on the wire path).
        if not is_record(payload) then
            return unknown
        end if
        if not json_encodable(payload) then
            return unknown
        end if
        if not _jwt_flat(payload) then
            return unknown
        end if
        body = json_encode(payload)
        signing_input = base64url_encode(header) + "." + base64url_encode(body)
        sig = base64url_encode(hmac_sha256(secret, signing_input))
        return signing_input + "." + sig
    end function

    function jwt_verify(token, secret)
        parts = split(token, ".")
        if len(parts) != 3 then
            return unknown
        end if
        signing_input = parts[0] + "." + parts[1]
        expected = base64url_encode(hmac_sha256(secret, signing_input))
        if not bytes_equal(expected, parts[2]) then
            return unknown
        end if
        body = base64url_decode(parts[1])
        if is_unknown(body) then
            return unknown
        end if
        payload = json_decode(body)
        if is_unknown(payload) then
            return unknown
        end if
        ' Enforce the exp claim (Unix seconds) when present.
        if has(payload, "exp") then
            if is_number(payload["exp"]) then
                if payload["exp"] < epoch() then
                    return unknown
                end if
            end if
        end if
        return payload
    end function

end library
