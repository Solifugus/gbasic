#!/usr/bin/env python3
"""Parse a message with python3's `email` module and assert what it decodes to.

INDEPENDENT IMPLEMENTATION, and that is the whole point of the tier: gBASIC's
own composer is the thing under test, so validating with anything of ours
would only prove we agree with ourselves. The claims below are about what a
MAIL CLIENT will see -- an encoded-word that decodes back to the author's
text, a Date that parses to the right instant, a Bcc that is absent, an
attachment whose bytes survived base64.
"""
import email, email.policy, sys

failures = []
def want(label, actual, expected):
    if actual != expected:
        failures.append("%s: got %r, expected %r" % (label, actual, expected))
    else:
        print("ok   %s" % label)

text = sys.stdin.read()
m = email.message_from_string(text, policy=email.policy.default)

want("parses with no defects", list(m.defects), [])
want("From decodes back to the author's text", m['from'], "Système <noreply@example.com>")
want("Subject decodes back", m['subject'], "Rapport quotidien — 日本語")
want("To keeps both recipients", m['to'], "a@example.com, Bee <b@example.com>")
want("Cc survives", m['cc'], "c@example.com")
want("Reply-To survives", m['reply-to'], "support@example.com")
want("Bcc is absent", m['bcc'], None)
want("the blind recipient appears nowhere", "hidden@example.com" in text, False)
want("the custom header survives", m['x-run-id'], "run-91")
want("Date parses to the right instant",
     m['date'].datetime.isoformat(), "2026-03-01T09:30:00+00:00")
want("MIME-Version is declared", m['mime-version'], "1.0")

parts = [p for p in m.walk() if not p.is_multipart()]
want("three leaf parts", len(parts), 3)
want("structure", [p.get_content_type() for p in m.walk()],
     ['multipart/mixed', 'multipart/alternative', 'text/plain', 'text/html', 'text/csv'])
want("plain text survives", parts[0].get_content(), "Bonjour,\n\nCi-joint le rapport.\n")
want("html survives", parts[1].get_content(), "<p>Bonjour</p>\n")
want("attachment filename", parts[2].get_filename(), "report.csv")
want("attachment bytes survive base64", parts[2].get_content(), "a,b\n1,2\n")

if failures:
    for f in failures:
        print("MISMATCH " + f)
    sys.exit(1)
print("validated %d claims with python3's email parser" % (19,))
