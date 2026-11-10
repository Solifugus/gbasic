# Mail design — composing and sending email

Status: **Shipped** (composer `stdlib/mail.bas`, transport `smtp` module;
reading a received message, `mail.parse`, §10, since book gap G59).

## 1. Why

gBASIC could not send email. Every program that needed a notification had to
shell out to `sendmail`, which means: a hard dependency on a local MTA, no way
to reach a relay that requires authentication, no error the program can read,
and a shell command built by string concatenation from whatever the program had
to hand. The Transward build hit this at its Phase 4 notifications and the only
route available was `process.run("sendmail", ...)`.

## 2. Two layers, and where each refusal lives

The split is not cosmetic. It is chosen so that **the whole composer is testable
with no network at all** — the same reason `stdlib/web.bas` holds the route
table and `webserver` holds the socket.

| Layer | Owns | Refuses |
|---|---|---|
| `stdlib/mail.bas` | RFC 5322 message text: headers, MIME structure, encodings | header injection, malformed addresses, structural-header collisions |
| `smtp` (native, libcurl) | the SMTP conversation: envelope, TLS, AUTH, DATA framing | control characters in envelope addresses, an empty recipient set, unknown options |

A control-character refusal deliberately does **not** echo the offending
address back. Printing it into the diagnostic would carry the caller's control
characters one layer along, into whatever reads the log.

Each layer refuses at the boundary it can actually see. The composer knows what
a header is; the transport knows what a wire line is. Neither can enforce the
other's rule, and a rule enforced in the wrong layer is a rule that can be
walked around.

**Line framing belongs to the transport, not the composer.** Two things follow
from that, and both are silent-corruption bugs if they land in the wrong place:

* **CRLF.** SMTP lines end `\r\n`. The composer works in ordinary text with
  `\n`, and `smtp.send` normalizes on the way out. An author writing
  `"line one\nline two"` gets a compliant message without knowing the rule.
* **Dot-stuffing — and it is libcurl's, which was measured, not assumed.** A
  line consisting of a single `.` ends the DATA phase, so a body containing one
  would be **truncated there with no error from anywhere**: the server accepts
  what it received and reports success. libcurl escapes the end-of-block
  sequence itself. Stuffing in `smtp.send` as well put **three** dots on the
  wire where the body had one, which the receiver un-stuffs to two — a
  corrupted message that is delivered, accepted, and perfectly plausible. Found
  by reading the test sink's wire capture. What curl looks for is the
  CRLF-framed sequence, which is why the normalization above has to happen and
  has to happen first.

* **The trailing newline.** It is transport framing for `7bit` (it terminates
  the last line, and the part join supplies it) and *content* for `base64`.
  Left alone, the same body decodes with a trailing newline in one encoding and
  without it in the other — a difference the author never asked for, decided by
  a rule they never see. The composer normalizes once, before choosing.

## 3. Header injection is refused, never escaped

A CR or LF in a subject, a display name, a recipient or a custom header value
lets whoever supplied that text write their own headers — a second `Bcc:`, a
forged `From:`, or a blank line followed by a body of their choosing. This is
the same defect class as HTTP response splitting, and it gets the same answer
the web layer already gives it: **refuse, and name the field.**

Escaping is not offered. There is no encoding of a newline that means "a
newline inside a header value" — a header value cannot contain one — so any
escaping scheme would be inventing a meaning for input that has none.

## 4. Encoding decisions

* **Subject** — RFC 2047 `=?utf-8?B?...?=` when it is not pure ASCII. An
  un-encoded non-ASCII subject arrives as mojibake in most clients: a plausible
  wrong answer, not an error.
* **Body** — sent verbatim as `7bit` when it is pure ASCII, has no line at or
  over 900 bytes, and holds no NUL; otherwise `base64` with
  `charset="utf-8"`. The ASCII test is `byte_count(s) = len(s)`, which is O(1)
  and exact: in UTF-8 a codepoint encodes to one byte only when it is below
  0x80. Keeping ordinary mail as readable 7bit is deliberate — it is what makes
  the wire capture in the test suite legible.
* **Attachments** — always `base64`, wrapped at 76 columns, with the filename
  in both `Content-Type; name=` and `Content-Disposition; filename=`.

## 5. Structure

| message has | structure |
|---|---|
| body only | single part |
| body + html | `multipart/alternative` |
| body + attachments | `multipart/mixed` |
| body + html + attachments | `mixed` wrapping `alternative` |

The boundary is random and **verified absent from every part before use**,
regenerating if it collides. A boundary that appears in the content splits the
message at the wrong place, and the result is a message that still parses.

## 6. Bcc

`bcc` recipients are added to the **envelope** and never to the headers. A Bcc
that reaches the headers discloses exactly what the author was trying to keep
private, and does so on every copy delivered. This is asserted directly.

## 7. Failure is a raise

`mail.compose` raises on malformed input, following `{USD}`: what the author
wrote cannot be represented, and the nearest legal neighbour is a different
message. `smtp.send` raises on a transport or relay failure, following
`sqlite`/`pg`/`odbc`, and carries the server's own reply text. Both are
catchable frame-locally with `on error goto next` (PLAT-ERR).

Any recipient the relay rejects fails the whole send. libcurl can be told to
continue past a rejected `RCPT TO`, and it is not: partial delivery reported as
success is the failure mode that costs the most to discover later.

## 8. TLS defaults

Two defaults are security-relevant and both would be silent if wrong, so both
are asserted directly:

* **Certificate verification is on.** `verify: false` exists for a self-signed
  relay and has to be asked for.
* **`starttls` does not downgrade.** Against a relay that does not offer
  STARTTLS the send *fails*; it does not continue in the clear. Otherwise a
  relay that merely stopped advertising STARTTLS would put every message, and
  the credentials, on the wire in plaintext with nothing to show for it.

## 9. Not in scope

Fetching mail (IMAP/POP3; reading a message once it has arrived is §10),
S/MIME and PGP, DKIM signing, address-header
folding beyond what `Subject` needs, and delivery-status parsing. Each is a
project of its own; none is needed to send a notification.

## 10. Reading a received message

`mail.parse(raw)` (book gap G59) reads a message as it arrived: headers,
addresses, date, every part with its transfer encoding undone, and the text a
person would read. Its decisions:

* **Nothing in a received message raises.** The composer refuses, because its
  input is the author's and a refusal tells the author what to fix. A received
  message is someone else's, and a part that cannot be read is no reason to
  lose the others. So every failure is a sentence in `problems` naming the
  part, and a text that cannot be decoded is `unknown` with its bytes kept in
  the part's `body`. A guess (reading an unknown charset as Latin-1, say) would
  be a plausible wrong answer, which is the kind nobody catches. The one
  exception is header bytes that are neither ASCII nor UTF-8: a header has no
  `body` to keep them in, so they are read as Latin-1 and that is a problem.
* **Which part is the text is Python's rule** (`email.message.get_body`): the
  first `text/plain` that is not an attachment, depth first, and only the
  start of a `multipart/related`. Any rule would do; this one is shared with
  the oracle, so the two can be held to each other.
* **The oracle is Python's `email` package**, reading the same bytes
  (`tests/run_mail_parse.sh`), and where it departs from the RFCs the RFC is
  followed and the suite says where: white space between adjacent encoded
  words in a display name (RFC 2047 6.2), trailing white space on a
  quoted-printable line (RFC 2045 6.7), and two-digit years 50-68
  (RFC 5322 4.3).
* **Dates are instants in UTC**, as every instant in gBASIC is (ruled
  2026-11-10), with the writer's offset beside them (`date_offset`). `compose`
  reads its `date` as UTC too and writes the header in `zone:` (default the
  system's), so a parsed date goes back out unmoved. Until that ruling
  `compose` read `date` as local time and the round trip moved by the
  machine's offset, which is how the question was found.
* **Quotation is a convention.** `mail.own_text` cuts at the first line
  starting `>`, an `On … wrote:` line or `-----Original Message-----`, says
  which, and names unquoted lines below the cut, since a reply written between
  quoted lines is the case the rule gets wrong. `format=flowed` (RFC 3676)
  makes quote depth explicit, and `mail.unflow` keeps depths apart.
* **Charsets** are the ones `transcode` reads, plus us-ascii: utf-8,
  iso-8859-1, windows-1252, utf-16. The rest are named, not read.
