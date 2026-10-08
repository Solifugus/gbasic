' A password hash is DATA: written by one machine, checked by another. On Linux
' password_hash is crypt(3) through libxcrypt; on Windows it is the vendored
' yescrypt (third_party/yescrypt), because libxcrypt is LGPL and gbasic.exe links
' none. Two implementations of one contract, so this file carries one hash MADE
' BY EACH, and whichever backend runs it must accept both:
'
'   LINUX_HASH    written by libxcrypt 4.5.1 (gBASIC on Ubuntu, 2026-10-03)
'   WINDOWS_HASH  written by the vendored yescrypt (gbasic.exe, same day)
'
' Both were also checked from OUTSIDE gBASIC, by perl's crypt() over the system
' libxcrypt: each verifies, and a password differing in one letter does not.
' So on Linux this file is libxcrypt reading the vendored code's output, and on
' Windows it is the vendored code reading libxcrypt's.
'
'     gbasic tests/windows/password_interop.bas     (from the repository root)
'
' "mismatches: 0" is a pass. A build with no password backend prints SKIP.

tally = { checks: 0, bad: 0 }

function ok(label, got, want)
    tally.checks = tally.checks + 1
    if string(got) = string(want) then
        print("ok   " + label)
    else
        print("MISMATCH " + label + ": got [" + string(got) + "] want [" + string(want) + "]")
        tally.bad = tally.bad + 1
    end if
end function

' "refused: <message>" when the call raised, otherwise the answer.
function verify_or_refusal(pw, h)
    on error goto next
    r = password_verify(pw, h)
    if error then
        m = error.message
        error.clear()
        return "refused: " + m
    end if
    return r
end function

function hash_or_refusal(pw)
    on error goto next
    r = password_hash(pw)
    if error then
        m = error.message
        error.clear()
        return "refused: " + m
    end if
    return r
end function

PW = "correct horse battery staple"
WRONG = "Correct horse battery staple"
LINUX_HASH = "$y$j9T$fGpSWSCTrdZaMT69NAhOq1$5HH7Q2Qs8VNW63J3cYrSvAiaznpE6uh9Zp/oPFIHGE1"
WINDOWS_HASH = "$y$j9T$emyFQR9p/Xen/HojV4GA./$pADU9H32/e3tI4sH1OxEKtcYoCH/ES7nS4xIqkJNPa7"
' sha512crypt, from perl's crypt() over libxcrypt. A GOOD hash of a scheme the
' vendored backend does not implement.
SHA512_HASH = "$6$gbasicvector$y.ijOURDub5b/769rTfMphR8BpM7c62qzp9U.YKfrmDnbEemgudN0R9y9LDSFWNDR7o7F179EZa5nYAdwyCsV/"

probe = hash_or_refusal("x")
if starts_with(string(probe), "refused:") then
    print("SKIP no password backend in this build: " + string(probe))
else
    print("-- a hash the OTHER backend wrote --")
    ok("libxcrypt's hash verifies", password_verify(PW, LINUX_HASH), true)
    ok("CONTROL: and one wrong letter does not", password_verify(WRONG, LINUX_HASH), false)
    ok("the vendored yescrypt's hash verifies", password_verify(PW, WINDOWS_HASH), true)
    ok("CONTROL: and one wrong letter does not", password_verify(WRONG, WINDOWS_HASH), false)

    print("-- a hash made here has the shape the other backend writes --")
    h = password_hash(PW)
    ok("the same scheme and cost, $y$j9T$", left(h, 7), "$y$j9T$")
    ok("the same length (16 salt bytes, 32 hash bytes)", len(h), len(LINUX_HASH))
    ok("it verifies", password_verify(PW, h), true)
    ok("CONTROL: a wrong password does not", password_verify(WRONG, h), false)
    ok("the salt is fresh each time", password_hash(PW) = h, false)
    ok("password_hash_cost reports the posture it hashes at", password_hash_cost().prefix, "$y$j9T$")

    print("-- a good hash is never answered false --")
    ' libxcrypt reads $6$ and answers true. The vendored backend cannot, and a
    ' false would tell the caller the PASSWORD was wrong, locking a real user
    ' out with nothing said, so it must refuse and name the scheme instead.
    s6 = verify_or_refusal(PW, SHA512_HASH)
    ok("sha512crypt: verified, or refused naming $6$",
       s6 = true or contains(string(s6), "this one is $6$"), true)
    ok("CONTROL: with a wrong password it is never TRUE",
       verify_or_refusal(WRONG, SHA512_HASH) = true, false)

    print("-- not a hash at all answers false on both backends --")
    ok("an empty hash", password_verify(PW, ""), false)
    ok("plain text", password_verify(PW, "not a hash"), false)

    print("-- a NUL would end the password, so it is refused --")
    ' crypt(3) takes a C string: "a<NUL>xyz" used to hash as "a", and then
    ' verify for "a<NUL>anything".
    nul_pw = "correct" + chr(0) + "horse"
    ok("password_hash refuses it",
       contains(string(hash_or_refusal(nul_pw)), "cannot contain a NUL"), true)
    ok("password_verify refuses it",
       contains(string(verify_or_refusal(nul_pw, LINUX_HASH)), "cannot contain a NUL"), true)
    ok("CONTROL: the text BEFORE the NUL is an ordinary password",
       left(string(hash_or_refusal("correct")), 7), "$y$j9T$")
end if

print("")
if starts_with(string(probe), "refused:") then
    print("SKIP")
else if tally.checks < 17 then
    print("BROKEN: only " + string(tally.checks) + " checks were counted")
    print("MISMATCHES: 1")
else
    print("checks: " + string(tally.checks))
    if tally.bad = 0 then
        print("mismatches: 0")
    else
        print("MISMATCHES: " + string(tally.bad))
    end if
end if
