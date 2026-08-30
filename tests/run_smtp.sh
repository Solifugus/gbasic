#!/usr/bin/env bash
# PLAT-MAIL: composing and sending email (docs/mail_design.md).
#
# gBASIC could not send mail at all, so a program that needed a notification
# shelled out to `sendmail` -- a hard dependency on a local MTA, no way to
# reach an authenticated relay, and no error the program could read.
#
# The split under test is the reason this suite can be thorough: composition
# is `stdlib/mail.bas`, pure gBASIC, so the whole of it is checkable WITH NO
# NETWORK; the `smtp` module owns only the wire. Each layer refuses at the
# boundary it can see -- headers here, SMTP commands there -- because a rule
# enforced in the wrong layer is a rule that can be walked around.
#
# Tiers:
#
#   compose    self-checking, not a golden: every check states its own
#              expected answer and prints ok or a MISMATCH naming both sides.
#              A golden would record whatever the composer emits AS expected,
#              and every defect here (a body sent 7bit that needed base64, a
#              Bcc reaching the headers, a boundary occurring inside a part)
#              produces a message that still looks like a message.
#   golden     the byte-exact rendering, with only the Message-ID and the
#              boundaries masked -- the two things that vary per run. The Date
#              is pinned by the fixture and TZ=UTC by the runner.
#   oracle     THE STRONGEST TIER: every composed message parsed by python3's
#              `email` module, an INDEPENDENT implementation, and asserted on
#              what a mail client will actually see -- encoded-words that
#              decode back to the author's text, a Date that parses to the
#              right instant, an attachment whose bytes survived base64.
#              Validating with anything of ours would only prove we agree
#              with ourselves.
#   refusal    14 refusals, each beside its NEAREST LEGAL NEIGHBOUR: a refusal
#              suite with no control is satisfied by refusing everything.
#   wire       through a sink that records the exact bytes. Three things no
#              functional test can see from outside: CRLF framing, DOT-STUFFING
#              (a body line of "." ends the DATA phase -- an unstuffed message
#              is TRUNCATED there, the server reports success, and nothing
#              raises anywhere), and Bcc in RCPT TO but not in DATA.
#   auth       credentials reach the relay, and a relay that demands auth from
#              a client with none refuses.
#   reject     a rejected recipient fails the WHOLE send, carrying the relay's
#              own words. Partial delivery reported as success is the failure
#              mode that costs the most to find out about later.
#   tls        implicit smtps and STARTTLS, plus the two security defaults
#              that would be silent if wrong: certificate verification is ON
#              unless asked otherwise, and `starttls` against a server that
#              does not offer it FAILS rather than sending in the clear.
#   valgrind
#
# Skips without python3 (the sink and the oracle); the TLS tier additionally
# needs openssl. Everything is loopback; nothing leaves the machine.
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

if ! command -v python3 >/dev/null 2>&1; then
    printf 'SKIP tests/run_smtp.sh (python3 is needed for the sink and the oracle)\n'
    exit 0
fi
if printf 'program main( args )\n load smtp\nend program\n' >/tmp/.gb_smtp_probe.bas 2>/dev/null &&
   ! GBASIC_PATH=stdlib ./gbasic /tmp/.gb_smtp_probe.bas 2>&1 | grep -q .; then
    :
else
    if GBASIC_PATH=stdlib ./gbasic /tmp/.gb_smtp_probe.bas 2>&1 | grep -q "not available in this build"; then
        rm -f /tmp/.gb_smtp_probe.bas
        printf 'SKIP tests/run_smtp.sh (built without libcurl, so there is no smtp module)\n'
        exit 0
    fi
fi
rm -f /tmp/.gb_smtp_probe.bas

scratch="$(mktemp -d)"
sinks=()
cleanup() {
    for pid in "${sinks[@]:-}"; do kill "$pid" 2>/dev/null || true; done
    rm -rf "$scratch"
}
trap cleanup EXIT

cases=0
fail() { printf 'FAIL %s\n' "$1"; exit 1; }
ok()   { cases=$((cases + 1)); printf 'PASS %s\n' "$1"; }

run() { GBASIC_PATH=stdlib ./gbasic "$@"; }

# Start a sink, wait for it to publish its port, and echo the port.
# NOTE start_sink runs inside $( ), so `fail` here would exit the SUBSHELL and
# the parent would sail on with an empty port -- which is not hypothetical: it
# interpolated nothing into the generated fixture and the suite then reported a
# gBASIC "syntax error, unexpected COMMA", sending the reader after a parser
# bug when a python sink had failed to start. So this echoes the port and
# nothing else, and every caller passes it through `require_port`, which runs
# in the PARENT and can actually stop the run. Timeout is generous because the
# observed failure was under full-gate load, not a dead sink.
start_sink() { # transcript count [extra args...]
    local transcript="$1"; shift
    local count="$1"; shift
    local portfile="$scratch/port.$RANDOM"
    python3 tests/smtp_sink.py --transcript "$transcript" --count "$count" "$@" \
        >"$portfile" 2>"$portfile.err" &
    sinks+=("$!")
    local waited=0
    while [ ! -s "$portfile" ]; do
        waited=$((waited + 1))
        if [ "$waited" -gt 400 ]; then
            # Return 0, NOT 1: the script runs under `set -e` and this is
            # inside $( ), so a nonzero status kills the run at the assignment
            # and the message below never prints. Echo a non-numeric sentinel
            # and let require_port do the failing, where it can be seen.
            printf 'sink-did-not-start(%s)' \
                "$(tr '\n' ' ' <"$portfile.err" 2>/dev/null)"
            return 0
        fi
        sleep 0.05
    done
    cat "$portfile"
}

# Runs in the parent, so it can stop the run. A port that is not a bare number
# is a sink that did not start, and saying so beats every downstream symptom.
require_port() { # port label
    case "$1" in
        ''|*[!0-9]*) fail "$2: the sink did not start (got '"$1"')" ;;
    esac
}

wait_transcript() { # path
    local waited=0
    while [ ! -s "$1" ]; do
        waited=$((waited + 1))
        [ "$waited" -gt 300 ] && fail "the sink never wrote a transcript"
        sleep 0.05
    done
}

# --------------------------------------------------------------- compose
run tests/smtp/compose_check.bas >"$scratch/out" 2>"$scratch/err" \
    || fail "compose (exited nonzero: $(cat "$scratch/err"))"
grep -q MISMATCH "$scratch/out" && fail "compose$(printf '\n%s' "$(grep MISMATCH "$scratch/out")")"
grep -q "COVERAGE SHORTFALL" "$scratch/out" && fail "compose (the tier stopped short of its own check count)"
diff -u tests/smtp/compose_check.out "$scratch/out" >"$scratch/diff" \
    || fail "compose$(printf '\n%s' "$(cat "$scratch/diff")")"
ok "compose: $(sed -n 's/^checks: //p' "$scratch/out") self-checking assertions"

# ---------------------------------------------------------------- golden
TZ=UTC GBASIC_PATH=stdlib ./gbasic tests/smtp/compose_dump.bas >"$scratch/dump" 2>"$scratch/err" \
    || fail "golden (exited nonzero: $(cat "$scratch/err"))"
diff -u tests/smtp/compose_dump.out "$scratch/dump" >"$scratch/diff" \
    || fail "golden$(printf '\n%s' "$(cat "$scratch/diff")")"
ok "golden: the rendered message is byte-exact"

# ---------------------------------------------------------------- oracle
TZ=UTC GBASIC_PATH=stdlib ./gbasic tests/smtp/emit_raw.bas >"$scratch/raw" 2>"$scratch/err" \
    || fail "oracle (composing failed: $(cat "$scratch/err"))"
python3 tests/smtp/validate.py <"$scratch/raw" >"$scratch/oracle" 2>&1 \
    || fail "oracle$(printf '\n%s' "$(cat "$scratch/oracle")")"
ok "oracle: $(sed -n 's/^validated \([0-9]*\) claims.*/\1/p' "$scratch/oracle") claims through python3's email parser"

# --------------------------------------------------------------- refusal
run tests/smtp/refusals.bas >"$scratch/out" 2>"$scratch/err" \
    || fail "refusal (exited nonzero: $(cat "$scratch/err"))"
diff -u tests/smtp/refusals.out "$scratch/out" >"$scratch/diff" \
    || fail "refusal$(printf '\n%s' "$(cat "$scratch/diff")")"
refused=$(grep -c '^refused' "$scratch/out")
accepted=$(grep -c '^ACCEPTED' "$scratch/out")
[ "$refused" -ge 14 ] || fail "refusal (only $refused refusals; the tier has shrunk)"
[ "$accepted" -ge 4 ]  || fail "refusal (only $accepted controls; a composer that refuses everything would pass)"
ok "refusal: $refused refusals, each beside one of $accepted legal neighbours"

# The transport's own boundary -- the envelope and the SMTP commands -- which
# is checked before a socket is opened, so no relay is involved.
run tests/smtp/send_refusals.bas >"$scratch/out" 2>"$scratch/err" \
    || fail "send-refusal (exited nonzero: $(cat "$scratch/err"))"
diff -u tests/smtp/send_refusals.out "$scratch/out" >"$scratch/diff" \
    || fail "send-refusal$(printf '\n%s' "$(cat "$scratch/diff")")"
sendrefused=$(grep -c '^refused' "$scratch/out")
[ "$sendrefused" -ge 16 ] || fail "send-refusal (only $sendrefused cases; the tier has shrunk)"
grep -q "by VALIDATION" "$scratch/out" \
    && fail "send-refusal (the control was refused by validation, so the tier proves nothing)"
ok "send-refusal: $((sendrefused - 1)) envelope and configuration refusals, plus a control"

# ------------------------------------------------------------------ wire
# Two messages. The first is ASCII on purpose: a non-ASCII body goes base64,
# which has no line starting with a dot, so a UTF-8 fixture cannot exercise
# dot-stuffing AT ALL -- it would pass on a build that never stuffed anything.
# The second carries the UTF-8, and proves base64 survives the transport.
port=$(start_sink "$scratch/wire.json" 2)
require_port "$port" "wire"
cat >"$scratch/wire.bas" <<EOF
program main( args )
    load mail
    load smtp
    cfg = { host: "127.0.0.1", port: $port, security: "plain" }
    body = "before the dot\n.\nafter the dot\n..double\nplain tail"
    m = mail.compose({ from: "alerts@example.com",
                       to: ["ops@example.com"],
                       bcc: ["audit@example.com"],
                       subject: "wire", body: body })
    r = smtp.send(cfg, m)
    print "code " + string(r.code) + " recipients " + string(r.recipients)

    u = mail.compose({ from: "alerts@example.com", to: ["ops@example.com"],
                       subject: "Rapport — 日本語", body: "café ☃" })
    r2 = smtp.send(cfg, u)
    print "utf8 code " + string(r2.code)
end program
EOF
run "$scratch/wire.bas" >"$scratch/out" 2>"$scratch/err" \
    || fail "wire (send failed: $(cat "$scratch/err"))"
wait_transcript "$scratch/wire.json"
python3 - "$scratch/wire.json" >"$scratch/wirecheck" <<'PY' || fail "wire$(printf '\n%s' "$(cat "$scratch/wirecheck")")"
import json, sys
t = json.load(open(sys.argv[1]))[0]
bad = []
def want(label, actual, expected):
    if actual != expected:
        bad.append("%s: got %r, expected %r" % (label, actual, expected))

want("envelope from", t["envelope_from"], "alerts@example.com")
want("bcc is in the envelope", t["recipients"],
     ["ops@example.com", "audit@example.com"])
want("bcc is not in the DATA", "audit@example.com" in t["data"], False)

# Every LF on the wire is part of a CRLF. A bare LF is not a compliant line
# ending and some relays mangle the message that follows one.
wire = t["data_wire"]
want("every line ends CRLF", wire.count("\n"), wire.count("\r\n"))

# The message the receiver reconstructs must be BYTE-IDENTICAL to what the
# program wrote. A line of "." unstuffed would have ENDED the DATA phase --
# the server accepts what it got and reports success, so the only evidence
# is the missing half of the message.
body = t["data"].split("\r\n\r\n", 1)[1]
want("dot-stuffed exactly once", body.split("\r\n")[:5],
     ["before the dot", ".", "after the dot", "..double", "plain tail"])
want("the message did not truncate at the dot", "after the dot" in body, True)
# One newline after the content, not two and not none. The 7bit and base64
# paths reach that ending by different routes, and before the normalization
# they disagreed -- the same body arrived with a trailing newline in one
# encoding and without it in the other.
want("7bit body ends with exactly one newline", body.endswith("plain tail\r\n"), True)
want("...and not two", body.endswith("plain tail\r\n\r\n"), False)
want("stuffing happened on the wire", "\r\n..\r\n" in wire, True)

# The second message: UTF-8 through base64, checked by decoding what the
# relay received rather than by looking at our own encoder's output.
import email, email.policy
u = json.load(open(sys.argv[1]))[1]
msg = email.message_from_string(u["data"].replace("\r\n", "\n"),
                                policy=email.policy.default)
want("utf-8 subject survives the wire", msg["subject"], "Rapport — 日本語")
want("utf-8 body survives the wire", msg.get_content(), "café ☃\n")

if bad:
    print("\n".join("MISMATCH " + b for b in bad)); sys.exit(1)
print("wire ok")
PY
ok "wire: CRLF framing, dot-stuffing exactly once, bcc in the envelope only, utf-8 intact"

# ------------------------------------------------------------------ auth
port=$(start_sink "$scratch/auth.json" 2 --require-auth)
require_port "$port" "auth"
cat >"$scratch/auth.bas" <<EOF
program main( args )
    load mail
    load smtp
    m = mail.compose({ from: "a@example.com", to: ["b@example.com"], subject: "s", body: "x" })
    r = smtp.send({ host: "127.0.0.1", port: $port, security: "plain",
                    username: "alerts", password: "s3cret" }, m)
    print "authenticated send: code " + string(r.code)
    on error goto failed
    r2 = smtp.send({ host: "127.0.0.1", port: $port, security: "plain" }, m)
    print "NOT REFUSED: the relay demanded auth and the send succeeded anyway"
    return
failed:
    print "no credentials refused"
end program
EOF
run "$scratch/auth.bas" >"$scratch/out" 2>"$scratch/err" \
    || fail "auth (exited nonzero: $(cat "$scratch/err"))"
grep -q "NOT REFUSED" "$scratch/out" && fail "auth$(printf '\n%s' "$(cat "$scratch/out")")"
grep -q "authenticated send: code 250" "$scratch/out" || fail "auth (the credentialed send failed)"
grep -q "no credentials refused" "$scratch/out" || fail "auth (the uncredentialed send was not refused)"
wait_transcript "$scratch/auth.json"
python3 -c "
import json,sys
t=json.load(open('$scratch/auth.json'))[0]
sys.exit(0 if t['auth'] and t['auth'][-2:] == ['alerts','s3cret'] else 1)
" || fail "auth (the credentials did not reach the relay)"
ok "auth: credentials reach the relay, and a relay demanding them refuses without"

# ---------------------------------------------------------------- reject
port=$(start_sink "$scratch/reject.json" 1 --reject-rcpt bad@example.com)
require_port "$port" "reject"
cat >"$scratch/reject.bas" <<EOF
program main( args )
    load mail
    load smtp
    m = mail.compose({ from: "a@example.com",
                       to: ["good@example.com", "bad@example.com"],
                       subject: "s", body: "x" })
    on error goto failed
    r = smtp.send({ host: "127.0.0.1", port: $port, security: "plain" }, m)
    print "NOT REFUSED: a rejected recipient was reported as success"
    return
failed:
    print error.message
end program
EOF
run "$scratch/reject.bas" >"$scratch/out" 2>"$scratch/err" \
    || fail "reject (exited nonzero: $(cat "$scratch/err"))"
grep -q "NOT REFUSED" "$scratch/out" && fail "reject (partial delivery reported as success)"
grep -q "bad@example.com" "$scratch/out" \
    || fail "reject (the error does not name WHICH recipient: $(cat "$scratch/out"))"
grep -q "no such user" "$scratch/out" \
    || fail "reject (the relay's own words did not reach the program: $(cat "$scratch/out"))"
wait_transcript "$scratch/reject.json"
python3 -c "
import json,sys
t=json.load(open('$scratch/reject.json'))[0]
sys.exit(0 if t.get('data') is None else 1)
" || fail "reject (the message was sent anyway to the recipients that were accepted)"
ok "reject: a refused recipient fails the whole send, in the relay's own words"

# ------------------------------------------------------------------- tls
if command -v openssl >/dev/null 2>&1; then
    openssl req -x509 -newkey rsa:2048 -keyout "$scratch/key.pem" -out "$scratch/cert.pem" \
        -days 2 -nodes -subj "/CN=127.0.0.1" -addext "subjectAltName=IP:127.0.0.1" \
        >/dev/null 2>&1 || fail "tls (could not generate a certificate)"

    for mode in tls starttls; do
        if [ "$mode" = tls ]; then flag=--tls; else flag=--starttls; fi
        port=$(start_sink "$scratch/$mode.json" 2 "$flag" "$scratch/cert.pem" "$scratch/key.pem")
        require_port "$port" "tls/$mode"
        cat >"$scratch/$mode.bas" <<EOF
program main( args )
    load mail
    load smtp
    m = mail.compose({ from: "a@example.com", to: ["b@example.com"], subject: "s", body: "x" })
    r = smtp.send({ host: "127.0.0.1", port: $port, security: "$mode", verify: false }, m)
    print "$mode: code " + string(r.code)
    on error goto failed
    r2 = smtp.send({ host: "127.0.0.1", port: $port, security: "$mode" }, m)
    print "NOT REFUSED: a self-signed certificate was accepted by default"
    return
failed:
    print "$mode: the default refused an unverifiable certificate"
end program
EOF
        run "$scratch/$mode.bas" >"$scratch/out" 2>"$scratch/err" \
            || fail "tls/$mode (exited nonzero: $(cat "$scratch/err"))"
        grep -q "NOT REFUSED" "$scratch/out" && fail "tls/$mode (verification is off by default)"
        grep -q "$mode: code 250" "$scratch/out" || fail "tls/$mode (the send failed: $(cat "$scratch/out"))"
        grep -q "refused an unverifiable" "$scratch/out" || fail "tls/$mode (no default verification)"
        ok "tls: $mode works, and certificate verification is on unless asked otherwise"
    done

    # The downgrade. `starttls` against a relay that does not offer it must
    # FAIL -- silently continuing in the clear would put the message, and the
    # credentials, on the wire in plaintext.
    port=$(start_sink "$scratch/downgrade.json" 1)
    require_port "$port" "tls/downgrade"
    cat >"$scratch/downgrade.bas" <<EOF
program main( args )
    load mail
    load smtp
    m = mail.compose({ from: "a@example.com", to: ["b@example.com"], subject: "s", body: "secret" })
    on error goto failed
    r = smtp.send({ host: "127.0.0.1", port: $port, security: "starttls" }, m)
    print "NOT REFUSED: the message went out in the clear"
    return
failed:
    print "downgrade refused"
end program
EOF
    run "$scratch/downgrade.bas" >"$scratch/out" 2>"$scratch/err" \
        || fail "tls/downgrade (exited nonzero: $(cat "$scratch/err"))"
    grep -q "NOT REFUSED" "$scratch/out" && fail "tls/downgrade (starttls fell back to plaintext)"
    wait_transcript "$scratch/downgrade.json"
    python3 -c "
import json,sys
t=json.load(open('$scratch/downgrade.json'))[0]
sys.exit(0 if t.get('data') is None else 1)
" || fail "tls/downgrade (the body reached a plaintext server)"
    ok "tls: starttls against a relay without it fails rather than sending in the clear"
else
    printf 'SKIP tls tiers (openssl is not installed)\n'
fi

# -------------------------------------------------------------- valgrind
if command -v valgrind >/dev/null 2>&1; then
    port=$(start_sink "$scratch/vg.json" 1)
    require_port "$port" "valgrind"
    cat >"$scratch/vg.bas" <<EOF
program main( args )
    load mail
    load smtp
    m = mail.compose({ from: "a@example.com", to: ["b@example.com"], cc: ["c@example.com"],
                       subject: "Rapport — 日本語", body: "café\n.\ntail",
                       html: "<p>x</p>",
                       attachments: [{ name: "a.csv", content: "1,2\n" }] })
    r = smtp.send({ host: "127.0.0.1", port: $port, security: "plain" }, m)
    print string(r.code)
end program
EOF
    GBASIC_PATH=stdlib valgrind -q --error-exitcode=9 --leak-check=no \
        ./gbasic "$scratch/vg.bas" >/dev/null 2>"$scratch/vg" \
        || fail "valgrind$(printf '\n%s' "$(cat "$scratch/vg")")"
    grep -q "Invalid" "$scratch/vg" && fail "valgrind$(printf '\n%s' "$(cat "$scratch/vg")")"
    ok "valgrind: no invalid access composing and sending"
else
    printf 'SKIP valgrind (not installed)\n'
fi

printf '\n%d checks passed\n' "$cases"
