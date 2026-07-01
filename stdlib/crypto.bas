' crypto.bas — ergonomic cryptography compositions in gBASIC.
'
' Built entirely on the crypto C builtins (docs/crypto_design.md Phases 1-2-4):
' base64url/hex encoders, hmac_sha256, sha256/sha512, bytes_equal, random_bytes,
' and aes_gcm_encrypt/decrypt. This is the Phase 3 layer — the safe-to-compose
' conveniences (hex digests, signed cookies, CSRF tokens, JWT/HS256, and
' nonce-managing symmetric encryption) that web apps reach for.
'
' A tiny JSON encoder/decoder for FLAT objects (string / number / bool / null
' values) lives here so JWT needs no new C dependency. It is not a general JSON
' library: nested objects and arrays are out of scope.
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

    ' ---- minimal flat-JSON (string / number / bool / null) -----------------

    function _json_escape(s)
        out = ""
        i = 0
        while i < len(s)
            ch = mid(s, i, 1)
            if ch = "\"" then
                out = out + "\\\""
            else
                if ch = "\\" then
                    out = out + "\\\\"
                else
                    if ch = "\n" then
                        out = out + "\\n"
                    else
                        if ch = "\t" then
                            out = out + "\\t"
                        else
                            if ch = "\u{000d}" then
                                out = out + "\\r"
                            else
                                out = out + ch
                            end if
                        end if
                    end if
                end if
            end if
            i = i + 1
        end while
        return out
    end function

    function json_encode(rec)
        if not is_record(rec) then
            return unknown
        end if
        ks = keys(rec)
        out = "{"
        i = 0
        while i < len(ks)
            k = ks[i]
            v = rec[k]
            if i > 0 then
                out = out + ","
            end if
            out = out + "\"" + _json_escape(k) + "\":"
            if is_string(v) then
                out = out + "\"" + _json_escape(v) + "\""
            else
                if is_number(v) then
                    out = out + string(v)
                else
                    if is_boolean(v) then
                        if v then
                            out = out + "true"
                        else
                            out = out + "false"
                        end if
                    else
                        if is_unknown(v) or is_nothing(v) then
                            out = out + "null"
                        else
                            return unknown
                        end if
                    end if
                end if
            end if
            i = i + 1
        end while
        return out + "}"
    end function

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

    function _json_is_num_char(ch)
        c = code(ch)
        if c >= code("0") and c <= code("9") then
            return true
        end if
        return ch = "-" or ch = "+" or ch = "." or ch = "e" or ch = "E"
    end function

    function _json_parse_number(s, i)
        start = i
        while i < len(s)
            if _json_is_num_char(mid(s, i, 1)) then
                i = i + 1
            else
                return { ok: true, value: number(mid(s, start, i - start)), pos: i }
            end if
        end while
        return { ok: true, value: number(mid(s, start, i - start)), pos: i }
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

    function jwt_encode(payload, secret)
        header = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}"
        body = json_encode(payload)
        if is_unknown(body) then
            return unknown
        end if
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
