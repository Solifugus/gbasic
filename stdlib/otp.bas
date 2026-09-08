' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' otp -- one-time passwords as a second factor.
'
' RFC 4226 (HOTP, counter-based) and RFC 6238 (TOTP, time-based). See
' docs/otp_design.md for why gBASIC owns this rather than delegating it, and
' for what this library deliberately does NOT do.
'
' THE DEFECTS THIS EXISTS TO PREVENT ARE NOT CRYPTOGRAPHIC. They are replay
' (a code stays valid for a whole step and can be presented twice), an
' over-generous skew window, and a missing lockout -- and each of them ships
' looking perfectly correct, because the output is always a plausible
' six-digit number.
'
' ONE OF THE THREE IS MADE STRUCTURAL: `check` REQUIRES the last counter the
' account accepted and RETURNS the one it matched, so a caller cannot reach
' the function without saying what was last used. The rule `reasoning.finding`
' follows for `search.width`, for the same reason -- the argument easiest to
' forget is the one that must not be optional.
library otp

    ' ---- defaults, in one place so the doc and the code cannot disagree -----
    function defaults()
        return { digits: 6, period: 30, algorithm: "sha1", skew: 1, t0: 0 }
    end function

    ' Merge caller options over the defaults. `unknown` and `nothing` both mean
    ' "said nothing", because a caller reaching a field that is not there gets
    ' `unknown` and one passing no options at all gets `nothing` -- and the two
    ' are different values in gBASIC (DOGFOOD, repeatedly).
    function _opts(o)
        d = defaults()
        if not is_record(o) then
            return d
        end if
        for each k in keys(o)
            if not contains(keys(d), k) then
                error "otp: no option '" + k + "'; it takes " + join(keys(d), ", ")
            end if
            d[k] = o[k]
        end for
        return d
    end function

    function _check_opts(o)
        if not contains(["sha1", "sha256", "sha512"], o.algorithm) then
            error "otp: algorithm must be sha1, sha256 or sha512 (most authenticator apps only do sha1)"
        end if
        if not is_number(o.digits) or o.digits < 6 or o.digits > 8 or o.digits != floor(o.digits) then
            error "otp: digits must be 6, 7 or 8"
        end if
        if not is_number(o.period) or o.period < 1 or o.period != floor(o.period) then
            error "otp: period must be a whole number of seconds"
        end if
        ' Every extra step of skew widens the brute-force window linearly, so a
        ' large value is refused rather than honoured. If clocks are further
        ' apart than this, the clocks are the problem.
        if not is_number(o.skew) or o.skew < 0 or o.skew > 10 or o.skew != floor(o.skew) then
            error "otp: skew must be between 0 and 10 steps; a wider window is a weaker factor, fix the clock instead"
        end if
        return nothing
    end function

    function _mac(algorithm, key, message)
        if algorithm = "sha1" then
            return hmac_sha1(key, message)
        end if
        if algorithm = "sha256" then
            return hmac_sha256(key, message)
        end if
        return hmac_sha512(key, message)
    end function

    ' The counter as 8 bytes, big-endian (RFC 4226 s5.1). Built with arithmetic
    ' rather than shifts because a counter outstays 32 bits: at a 30-second
    ' step, 2^32 steps is the year 6053, but the RFC's own test vectors already
    ' reach 666666666 and nothing here should quietly wrap.
    function _counter_bytes(counter)
        b = []
        n = counter
        for i = 1 to 8
            append(b, mod(n, 256))
            n = floor(n / 256)
        end for
        ' built least-significant first, so reverse for big-endian
        out = []
        for i = 8 to 1 step -1
            append(out, b[i - 1])
        end for
        return from_bytes(out)
    end function

    ' RFC 4226 s5.3 dynamic truncation.
    function _truncate(mac, digits)
        last = byte_at(mac, byte_count(mac) - 1)
        offset = band(last, 15)
        v = shl(band(byte_at(mac, offset), 127), 24)
        v = v + shl(byte_at(mac, offset + 1), 16)
        v = v + shl(byte_at(mac, offset + 2), 8)
        v = v + byte_at(mac, offset + 3)
        p = 1
        for i = 1 to digits
            p = p * 10
        end for
        return _pad(string(mod(v, p)), digits)
    end function

    function _pad(s, width)
        out = s
        while len(out) < width
            out = "0" + out
        end while
        return out
    end function

    ' ---- the two RFCs ------------------------------------------------------

    ' otp.hotp(secret, counter [, options]) -> the code as TEXT.
    ' Text, not a number: a code may begin with 0 and 072653 is not 72653.
    function hotp(secret, counter, options = nothing)
        o = _opts(options)
        _check_opts(o)
        if not is_number(counter) or counter < 0 or counter != floor(counter) then
            error "otp: the counter must be a whole number, zero or above"
        end if
        key = base32_decode(secret)
        if is_unknown(key) then
            error "otp: the secret is not valid base32"
        end if
        return _truncate(_mac(o.algorithm, key, _counter_bytes(counter)), o.digits)
    end function

    ' otp.totp(secret, at [, options]) -> the code at that instant.
    ' `at` is epoch SECONDS. Taken as an argument rather than read from the
    ' clock so that a caller can compute the code for a stated moment -- which
    ' is what enrollment confirmation and every test vector need.
    function totp(secret, at, options = nothing)
        o = _opts(options)
        _check_opts(o)
        return hotp(secret, counter_at(at, options), options)
    end function

    ' The step number an instant falls in. Exposed because a caller storing
    ' `last_counter` has to be able to talk about it.
    function counter_at(at, options = nothing)
        o = _opts(options)
        _check_opts(o)
        if not is_number(at) then
            error "otp: `at` must be epoch seconds (use epoch(now()))"
        end if
        return floor((at - o.t0) / o.period)
    end function

    ' ---- verification ------------------------------------------------------

    ' otp.check(secret, typed, last_counter, at [, options])
    '     -> { ok, counter, reason }
    '
    ' `last_counter` HAS NO DEFAULT, deliberately. Replay is the defect this
    ' library most exists to prevent, and a caller cannot reach this function
    ' without stating what the account last accepted. Pass 0 at enrollment,
    ' when nothing has been accepted yet -- which is honest, and is VISIBLE in
    ' the source to a reviewer, unlike a hidden default.
    '
    ' On success the caller MUST store `counter`. Not doing so leaves the code
    ' replayable for the rest of its step, which is the whole failure.
    function check(secret, typed, last_counter, at, options = nothing)
        o = _opts(options)
        _check_opts(o)
        if not is_number(last_counter) or last_counter < 0 or last_counter != floor(last_counter) then
            error "otp: last_counter must be a whole number; pass 0 when nothing has been accepted yet"
        end if
        given = _normalise(typed)
        if len(given) != o.digits then
            return { ok: false, counter: last_counter, reason: "malformed" }
        end if
        here = counter_at(at, options)
        first = here - o.skew
        if first < 0 then
            first = 0
        end if
        matched = -1
        ' EVERY candidate is compared, and the loop is NOT cut short on a hit.
        ' Returning early would make the reply time depend on WHICH step
        ' matched, which is a smaller leak than a byte-wise compare and is
        ' free to avoid.
        for c = first to here + o.skew
            if bytes_equal(hotp(secret, c, options), given) then
                matched = c
            end if
        end for
        if matched < 0 then
            return { ok: false, counter: last_counter, reason: "no_match" }
        end if
        ' THE REPLAY REFUSAL. A code already spent is refused even though it is
        ' arithmetically correct, which is exactly why this cannot be left to
        ' the caller to remember.
        if matched <= last_counter then
            return { ok: false, counter: last_counter, reason: "replayed" }
        end if
        return { ok: true, counter: matched, reason: "ok" }
    end function

    ' Authenticator apps display "123 456". Spaces are stripped so a user
    ' typing what they see is not told they are wrong; nothing else is.
    function _normalise(typed)
        if not is_string(typed) then
            return ""
        end if
        return replace(replace(typed, " ", ""), "-", "")
    end function

    ' ---- enrollment --------------------------------------------------------

    ' A new shared secret, base32, from /dev/urandom. 20 bytes is RFC 4226's
    ' recommended length and matches the HMAC-SHA1 block digest size.
    function secret(bytes_wanted = 20)
        if not is_number(bytes_wanted) or bytes_wanted < 16 or bytes_wanted > 64 then
            error "otp: a secret must be between 16 and 64 bytes"
        end if
        return base32_encode(random_bytes(bytes_wanted))
    end function

    ' The otpauth:// URI an authenticator app scans as a QR code.
    ' spec: { issuer, account, secret } plus any of the options.
    function uri(spec)
        if not is_record(spec) then
            error "otp: uri expects a record"
        end if
        for each need in ["issuer", "account", "secret"]
            if not has(spec, need) then
                error "otp: uri needs " + need
            end if
            if not is_string(spec[need]) or len(spec[need]) = 0 then
                error "otp: uri's " + need + " must be a non-empty string"
            end if
        end for
        o = defaults()
        for each k in keys(o)
            if has(spec, k) then
                o[k] = spec[k]
            end if
        end for
        _check_opts(o)
        label = _percent(spec.issuer) + ":" + _percent(spec.account)
        parts = ["secret=" + spec.secret,
                 "issuer=" + _percent(spec.issuer),
                 "algorithm=" + upper(o.algorithm),
                 "digits=" + string(o.digits),
                 "period=" + string(o.period)]
        return "otpauth://totp/" + label + "?" + join(parts, "&")
    end function

    ' Percent-encoding, private and deliberately conservative: everything that
    ' is not unreserved is escaped. gBASIC has NO url_encode builtin (recorded
    ' in DOGFOOD) -- this is encode-only over text we are placing into a URI we
    ' construct, which is why a local helper is acceptable here where a shared
    ' decoder of untrusted input would not be.
    function _percent(s)
        safe = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~"
        out = []
        i = 0
        while i < byte_count(s)
            b = byte_at(s, i)
            ch = from_bytes([b])
            if contains(safe, ch) then
                append(out, ch)
            else
                append(out, "%" + upper(_pad(hex_encode(ch), 2)))
            end if
            i = i + 1
        end while
        return join(out, "")
    end function

    ' Single-use recovery codes. Returned in the clear ONCE, for the user to
    ' write down; the caller hashes them with password_hash before storing, the
    ' same treatment a password gets, because a stolen recovery list is a
    ' bypass of the whole factor.
    function recovery_codes(how_many = 10)
        if not is_number(how_many) or how_many < 1 or how_many > 50 then
            error "otp: recovery_codes takes between 1 and 50"
        end if
        out = []
        for i = 1 to how_many
            append(out, lower(hex_encode(random_bytes(5))))
        end for
        return out
    end function

end library
