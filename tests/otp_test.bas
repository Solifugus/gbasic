' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' otp -- RFC 4226 (HOTP) and RFC 6238 (TOTP). See docs/otp_design.md.
'
' SELF-CHECKING RATHER THAN GOLDEN, and here that is forced: every defect in
' this library produces a PLAUSIBLE SIX-DIGIT NUMBER. A golden would record a
' wrong code as the expected output and defend it forever, and no reader could
' tell 081804 from 081805 by eye.
'
' THE ORACLE IS EXTERNAL AND PUBLISHED: RFC 4226 Appendix D and RFC 6238
' Appendix B. The values below were computed independently (python3
' hmac/hashlib) BEFORE this library existed and agree with the printed tables,
' so this file asserts what the RFCs say rather than what we produced.

load otp

tally = { checks: 0, mismatches: 0 }

function check(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print "ok   " + label
    else
        tally.mismatches = tally.mismatches + 1
        print "MISMATCH " + label + ": got " + string(got) + ", want " + string(want)
    end if
    return nothing
end function

print "-- RFC 4226 Appendix D: HOTP, 6 digits"
s = base32_encode("12345678901234567890")
hotp_want = ["755224","287082","359152","969429","338314","254676","287922","162583","399871","520489"]
for i = 0 to 9
    check("counter " + string(i), otp.hotp(s, i), hotp_want[i])
end for

print ""
print "-- RFC 6238 Appendix B: TOTP, 8 digits, all three algorithms"
' THE SEEDS DIFFER BY ALGORITHM and that is the trap the vectors carry:
' reusing the 20-byte SHA1 seed for SHA256/SHA512 gives wrong answers, and the
' correction then gets applied to the code instead of to the seed.
s1   = base32_encode("12345678901234567890")
s256 = base32_encode("12345678901234567890123456789012")
s512 = base32_encode("1234567890123456789012345678901234567890123456789012345678901234")
times = [59, 1111111109, 1111111111, 1234567890, 2000000000, 20000000000]
w1   = ["94287082","07081804","14050471","89005924","69279037","65353130"]
w256 = ["46119246","68084774","67062674","91819424","90698825","77737706"]
w512 = ["90693936","25091201","99943326","93441116","38618901","47863826"]
for i = 0 to 5
    t = times[i]
    check("sha1   t=" + string(t), otp.totp(s1, t, { digits: 8, algorithm: "sha1" }), w1[i])
    check("sha256 t=" + string(t), otp.totp(s256, t, { digits: 8, algorithm: "sha256" }), w256[i])
    check("sha512 t=" + string(t), otp.totp(s512, t, { digits: 8, algorithm: "sha512" }), w512[i])
end for
' t=20000000000 puts the counter at 666666666, past what 32 bits would hold if
' the counter were packed carelessly -- which is why that row is not decoration.
check("the counter outgrows 32 bits", otp.counter_at(20000000000), 666666666)

print ""
print "-- RFC 4648: base32, both directions"
b32 = [["", ""], ["f","MY======"], ["fo","MZXQ===="], ["foo","MZXW6==="],
       ["foob","MZXW6YQ="], ["fooba","MZXW6YTB"], ["foobar","MZXW6YTBOI======"]]
for each p in b32
    check("encode " + string(p[0]), base32_encode(p[0]), p[1])
    check("decode " + string(p[1]), base32_decode(p[1]), p[0])
end for
' Case and spacing are the ordinary noise in a hand-transcribed secret, and a
' decoder that refused them would turn a usability problem into a lockout.
check("lower case decodes", base32_decode("mzxw6ytboi======"), "foobar")
check("spacing decodes", base32_decode("MZXW 6YTB OI"), "foobar")
' unknown, NOT "" -- "not base32" and "empty" are different answers.
check("invalid is unknown", is_unknown(base32_decode("MZXW6YTB!")), true)
check("and empty is not unknown", is_unknown(base32_decode("")), false)

print ""
print "-- REPLAY: the defect this library most exists to prevent"
' THE LOAD-BEARING TIER. A code stays valid for its whole 30-second step, so
' being arithmetically correct is not enough to be accepted twice. Asserted as
' a DIFFERENCE with its control, because "accepted" alone passes on a library
' with no replay guard at all, and "refused" alone passes on one that refuses
' everything.
at = 1111111109
code = otp.totp(s, at)
r1 = otp.check(s, code, 0, at)
check("a fresh code is accepted", r1.ok, true)
check("and reports the counter to store", r1.counter, 37037036)
r2 = otp.check(s, code, r1.counter, at)
check("THE SAME CODE IS THEN REFUSED", r2.ok, false)
check("and says why", r2.reason, "replayed")
' THE CONTROL: the next step's code still works, so the guard is a guard and
' not a wall.
r3 = otp.check(s, otp.totp(s, at + 30), r1.counter, at + 30)
check("the next step is accepted", r3.ok, true)
check("with the counter advanced", r3.counter, 37037037)

print ""
print "-- SKEW: bounded, and asserted as a difference"
check("one step behind is accepted", otp.check(s, otp.totp(s, at - 30), 0, at).ok, true)
check("two steps behind is not", otp.check(s, otp.totp(s, at - 60), 0, at).ok, false)
check("one step ahead is accepted", otp.check(s, otp.totp(s, at + 30), 0, at).ok, true)
check("a wider window can be asked for", otp.check(s, otp.totp(s, at - 60), 0, at, { skew: 2 }).ok, true)
check("and skew 0 accepts only the current step", otp.check(s, otp.totp(s, at - 30), 0, at, { skew: 0 }).ok, false)

print ""
print "-- what a user actually types"
check("spaces are stripped", otp.check(s, "081 804", 0, at).ok, true)
check("a short code is malformed, not a miss", otp.check(s, "0818", 0, at).reason, "malformed")
check("a wrong code is a miss", otp.check(s, "000000", 0, at).reason, "no_match")

print ""
print "-- enrollment"
sec = otp.secret()
check("a secret is 20 bytes", byte_count(base32_decode(sec)), 20)
check("and two secrets differ", sec = otp.secret(), false)
u = otp.uri({ issuer: "Acme Corp", account: "a@b.com", secret: sec })
check("the uri is otpauth", starts_with(u, "otpauth://totp/"), true)
check("the issuer is percent-encoded", contains(u, "Acme%20Corp"), true)
check("and so is the account", contains(u, "a%40b.com"), true)
check("it carries the secret", contains(u, "secret=" + sec), true)
rc = otp.recovery_codes(5)
check("five recovery codes", count(rc), 5)
check("all different", count(unique(rc)), 5)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
