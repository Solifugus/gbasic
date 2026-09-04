' ldap -- bind and search (docs/ldap_design.md).
'
' SELF-CHECKING. This is an AUTHENTICATION path, so the failure that matters is
' not a crash: it is a bind that says the wrong thing. A directory that is down
' reported as a bad password locks every user out and tells the operator
' nothing; a bad password reported as "unreachable" is worse still.
'
' THE LOAD-BEARING TIER IS DISTINGUISHABILITY. It is the whole reason the ask
' was raised and the reason bind returns a VALUE rather than raising.
'
' Ports arrive in the environment rather than argv: argv needs `program
' main(args)`, and a program block hoists DECLARATIONS but does not run
' top-level statements, so the shared `tally` a check helper mutates would not
' exist yet (PLAT-GUARD). A plain script keeps the fixture in the same shape as
' every other one here.
load ldap

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

plain_port = number(env("LDAP_PLAIN_PORT"))
ldaps_port = number(env("LDAP_TLS_PORT"))
ca_file = env("LDAP_CA_FILE")
dead_port = number(env("LDAP_DEAD_PORT"))

alice = "cn=alice,ou=people,dc=example,dc=com"

' --- TIER: authentication ------------------------------------------------
c = ldap.connect({ host: "127.0.0.1", port: plain_port, security: "plain" })

good = ldap.bind(c, alice, "correct horse")
check("a correct password binds", good.ok, true)
check("  with no reason to report", good.reason, "")

bad = ldap.bind(c, alice, "wrong")
check("a wrong password does not", bad.ok, false)
check("  and says why", bad.reason, "invalid_credentials")
check("  carrying the directory's own result code", bad.code, 49)

' An empty password is an UNAUTHENTICATED bind: the directory answers SUCCESS
' and a caller that did not know would have logged the user in. This is the
' classic way an LDAP login accepts everybody.
empty = ldap.bind(c, alice, "")
check("an empty password is refused rather than sent", empty.ok, false)
' ITS OWN REASON. Both fail closed, but a caller cannot otherwise tell "I
' passed an empty password" -- their own bug -- from "the directory rejected
' these credentials". Reported by gdash from a real directory.
check("  with its own reason", empty.reason, "empty_password")
check("  distinguishable from a rejected credential", empty.reason != bad.reason, true)
check("  explaining what it would otherwise be",
        contains(empty.message, "unauthenticated bind"), true)

' --- TIER: DISTINGUISHABILITY, the reason this module exists -------------
dead = ldap.connect({ host: "127.0.0.1", port: dead_port, security: "plain" })
down = ldap.bind(dead, alice, "correct horse")
check("an unreachable directory does not bind", down.ok, false)
check("  and is NOT reported as a bad password", down.reason, "unreachable")
check("so the two failures are distinguishable", bad.reason != down.reason, true)

untrusted = ldap.connect({ host: "127.0.0.1", port: ldaps_port, security: "ldaps" })
tls = ldap.bind(untrusted, alice, "correct horse")
check("an untrusted certificate does not bind", tls.ok, false)
check("  and is its own reason, not `unreachable`", tls.reason, "tls_failed")
check("so a certificate problem is distinguishable from a network one",
        tls.reason != down.reason, true)
check("and from a bad password", tls.reason != bad.reason, true)

' A TLS FAILURE ARRIVES BY THE SAME ROUTE AS EVERY OTHER OPERATIONAL FAILURE,
' whichever security mode was declared. StartTLS used to RAISE from `connect`
' while LDAPS answered from `bind` -- the same condition behaving differently
' depending on a configuration field, with the documented contract holding for
' two modes out of three. gdash found it against a real directory and named the
' consequence: a caller who guards `bind`, as the documentation steers them to,
' takes the raise, and a raise inside a web handler kills the worker on EVERY
' login attempt instead of showing "sign-in is unavailable".
on error goto next
st = ldap.connect({ host: "127.0.0.1", port: plain_port, security: "starttls" })
check("StartTLS against a server that cannot do it does NOT raise from connect",
      error = false, true)
error.clear()
on error stop
st_bind = ldap.bind(st, alice, "correct horse")
check("  it answers from bind, like everything else", st_bind.ok, false)
check("  with tls_failed, which was documented and previously unreachable here",
      st_bind.reason, "tls_failed")
ldap.close(st)

' --- TIER: search ---------------------------------------------------------
rows = ldap.search(c, { base: "ou=people,dc=example,dc=com", scope: "sub",
                          filter: "(uid=alice)", attributes: ["cn", "memberOf"] })
check("a search finds the entry", count(rows), 1)
check("  and returns its DN", rows[0].dn, alice)
check("  with the attributes asked for", join(sort(keys(rows[0].attributes)), ","),
        "cn,memberOf")

' ALWAYS AN ARRAY, even for one value. `memberOf` is multi-valued and `cn` is
' not, and a caller that special-cases the two is correct until the day a
' user joins a second group.
check("a single-valued attribute is still an array",
        type(rows[0].attributes["cn"]), "array")
check("  of one", count(rows[0].attributes["cn"]), 1)
check("a multi-valued one carries all of them",
        count(rows[0].attributes["memberOf"]), 2)
check("  in full", contains(rows[0].attributes["memberOf"],
                              "cn=finance,ou=groups,dc=example,dc=com"), true)

' An empty attribute list means "everything", as LDAP defines it -- the
' control that shows the list is actually being sent.
every = ldap.search(c, { base: "dc=example,dc=com", scope: "sub",
                           filter: "(uid=alice)", attributes: [] })
check("an empty attribute list returns them all",
        count(keys(every[0].attributes)) > 2, true)

' NO MATCH IS AN EMPTY ARRAY, NOT AN ERROR. "This user does not exist" is an
' ordinary answer on a login path.
none = ldap.search(c, { base: "dc=example,dc=com", scope: "sub",
                          filter: "(uid=nobody)", attributes: ["cn"] })
check("a search that matches nothing is an empty array", count(none), 0)

' --- TIER: TLS that works -------------------------------------------------
' The control for the untrusted case above: without it, a module that failed
' every TLS connection would pass.
trusted = ldap.connect({ host: "127.0.0.1", port: ldaps_port,
                           security: "ldaps", ca_file: ca_file })
ok_tls = ldap.bind(trusted, alice, "correct horse")
check("naming the CA makes the same certificate acceptable", ok_tls.ok, true)

' And verification can be turned off, but only by saying so.
skipped = ldap.connect({ host: "127.0.0.1", port: ldaps_port,
                           security: "ldaps", verify: false })
ok_skip = ldap.bind(skipped, alice, "correct horse")
check("verification can be waived explicitly", ok_skip.ok, true)

' --- TIER: refusals -------------------------------------------------------
on error goto next

x = ldap.connect({ host: "127.0.0.1", port: plain_port })
check("a connection with no declared security is refused",
        contains(error.message, "there is no default"), true)
error.clear()

x = ldap.connect({ host: "127.0.0.1", port: plain_port, security: "maybe" })
check("an unknown security is refused by name",
        contains(error.message, "must be \"ldaps\", \"starttls\" or \"plain\""), true)
error.clear()

x = ldap.connect({ port: plain_port, security: "plain" })
check("a connection with no host is refused",
        contains(error.message, "needs a host"), true)
error.clear()

x = ldap.search(c, { base: "dc=example,dc=com", scope: "sideways",
                       filter: "(uid=alice)" })
check("an unknown scope is refused",
        contains(error.message, "scope must be"), true)
error.clear()

x = ldap.search(c, { scope: "sub", filter: "(uid=alice)" })
check("a search with no base is refused",
        contains(error.message, "needs a base DN"), true)
error.clear()

ldap.close(c)
x = ldap.bind(c, alice, "correct horse")
check("a closed connection is refused rather than reconnected",
        contains(error.message, "connection is closed"), true)
error.clear()

x = ldap.bind(dead, 42, "pw")
check("a non-string DN is refused",
        contains(error.message, "as strings"), true)
error.clear()

on error stop

' Closing twice is not an error -- an application unwinding should not have
' to track whether it already did.
check("closing an already-closed connection is fine", ldap.close(c), true)
ldap.close(dead)
ldap.close(untrusted)
ldap.close(trusted)
ldap.close(skipped)

print "checks: " + string(tally.checks)
print "mismatches: " + string(tally.mismatches)
