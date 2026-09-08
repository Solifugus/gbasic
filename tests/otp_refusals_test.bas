' SPDX-License-Identifier: Apache-2.0
' Copyright 2026 Matthew C. Tedder. See LICENSE and LICENSING.md.
'
' otp refusals, EACH BESIDE ITS NEAREST LEGAL NEIGHBOUR -- because a refusal
' suite with no controls is satisfied by a library that refuses everything,
' which is the failure mode this shape exists to exclude.

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

s = base32_encode("12345678901234567890")
on error goto next

print "-- the secret"
otp.hotp("not!base32", 0)
check("a secret that is not base32 is refused", contains(error.message, "not valid base32"), true)
error.clear()
check("CONTROL: a real secret works", len(otp.hotp(s, 0)), 6)

print ""
print "-- the counter"
otp.hotp(s, -1)
check("a negative counter is refused", contains(error.message, "whole number"), true)
error.clear()
otp.hotp(s, 1.5)
check("a fractional counter is refused", contains(error.message, "whole number"), true)
error.clear()
check("CONTROL: counter 0 is legal", otp.hotp(s, 0), "755224")

print ""
print "-- options"
otp.hotp(s, 0, { algorithm: "md5" })
check("an unknown algorithm is refused", contains(error.message, "sha1, sha256 or sha512"), true)
check("and the message warns about app support", contains(error.message, "authenticator"), true)
error.clear()
otp.hotp(s, 0, { digits: 4 })
check("4 digits is refused", contains(error.message, "6, 7 or 8"), true)
error.clear()
otp.hotp(s, 0, { digits: 9 })
check("9 digits is refused", contains(error.message, "6, 7 or 8"), true)
error.clear()
otp.hotp(s, 0, { period: 0 })
check("a zero period is refused", contains(error.message, "whole number of seconds"), true)
error.clear()
' A WIDE SKEW IS A WEAKER FACTOR, so it is refused rather than honoured: every
' extra step widens the brute-force window linearly.
otp.check(s, "000000", 0, 100, { skew: 99 })
check("an enormous skew is refused", contains(error.message, "weaker factor"), true)
error.clear()
' Unknown fields refused BY NAME -- the rule webserver.listen already follows,
' because a silently ignored `digets: 8` leaves a factor weaker than asked for.
otp.hotp(s, 0, { digets: 8 })
check("a misspelled option is refused BY NAME", contains(error.message, "no option 'digets'"), true)
check("and lists what it does take", contains(error.message, "digits"), true)
error.clear()
check("CONTROL: the same option spelled right is honoured", len(otp.hotp(s, 0, { digits: 8 })), 8)
check("CONTROL: skew 10 is the widest allowed", otp.check(s, otp.totp(s, 100), 0, 100, { skew: 10 }).ok, true)

print ""
print "-- last_counter, which has no default"
' THE ARGUMENT THAT MUST NOT BE OPTIONAL. A caller cannot reach `check`
' without stating what the account last accepted.
otp.check(s, "000000", -1, 100)
check("a negative last_counter is refused", contains(error.message, "last_counter"), true)
check("and the message says what 0 means", contains(error.message, "nothing has been accepted yet"), true)
error.clear()
check("CONTROL: 0 is legal and means nothing accepted yet", otp.check(s, otp.totp(s, 100), 0, 100).ok, true)

print ""
print "-- enrollment"
otp.secret(4)
check("a 4-byte secret is refused", contains(error.message, "between 16 and 64"), true)
error.clear()
check("CONTROL: 16 bytes is the floor and is allowed", byte_count(base32_decode(otp.secret(16))), 16)
otp.uri({ issuer: "a", account: "b" })
check("a uri without a secret is refused", contains(error.message, "uri needs secret"), true)
error.clear()
otp.uri({ issuer: "", account: "b", secret: s })
check("an empty issuer is refused", contains(error.message, "non-empty"), true)
error.clear()
check("CONTROL: a complete spec builds", starts_with(otp.uri({ issuer: "a", account: "b", secret: s }), "otpauth://"), true)
otp.recovery_codes(0)
check("zero recovery codes is refused", contains(error.message, "between 1 and 50"), true)
error.clear()
check("CONTROL: one is allowed", count(otp.recovery_codes(1)), 1)

print ""
print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
