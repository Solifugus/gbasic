#!/usr/bin/env python3
"""A minimal SMTP sink that records the EXACT WIRE BYTES.

Not a mail server. It exists so the suite can assert things no functional test
can reach from the outside: that the payload was CRLF-framed, that a body line
of "." arrived dot-stuffed (and so did not truncate the message), that Bcc
recipients appear in RCPT TO and nowhere in the DATA, and that a relay's
refusal reaches the program as a raise carrying the server's own reply.

Prints its port on stdout, then serves one connection per --count and writes a
JSON transcript to --transcript.

  --reject-rcpt ADDR   answer 550 to that RCPT TO
  --require-auth       refuse MAIL FROM until AUTH has succeeded
  --tls CERT KEY       serve implicit TLS (smtps)
  --starttls CERT KEY  advertise and accept STARTTLS
"""
import argparse, base64, json, socket, ssl, sys, threading

def address_of(argument):
    """`<a@b> SIZE=285` -> `a@b`. curl appends ESMTP parameters after the path."""
    argument = argument.strip()
    if argument.startswith("<"):
        end = argument.find(">")
        if end > 0:
            return argument[1:end]
    return argument.split(" ")[0].strip("<>")

def read_line(f):
    line = f.readline()
    if not line:
        raise EOFError
    return line

def serve_one(conn, args, out):
    session = {"envelope_from": None, "recipients": [], "data": None,
               "auth": None, "starttls": False, "commands": []}
    f = conn.makefile("rwb")
    def say(text):
        f.write(text.encode() + b"\r\n"); f.flush()

    say("220 sink.test ESMTP gbasic-sink")
    authed = not args.require_auth
    while True:
        try:
            raw = read_line(f)
        except EOFError:
            break
        line = raw.decode("utf-8", "replace").rstrip("\r\n")
        session["commands"].append(line.split(" ")[0].upper())
        upper = line.upper()

        if upper.startswith("EHLO") or upper.startswith("HELO"):
            caps = ["250-sink.test"]
            if args.starttls and not session["starttls"]:
                caps.append("250-STARTTLS")
            caps.append("250-AUTH PLAIN LOGIN")
            caps.append("250 SIZE 10485760")
            for c in caps:
                say(c)
        elif upper == "STARTTLS" and args.starttls:
            say("220 Ready to start TLS")
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ctx.load_cert_chain(args.starttls[0], args.starttls[1])
            conn = ctx.wrap_socket(conn, server_side=True)
            f = conn.makefile("rwb")
            session["starttls"] = True
        elif upper.startswith("AUTH PLAIN"):
            parts = line.split(" ", 2)
            if len(parts) == 3:
                blob = base64.b64decode(parts[2]).split(b"\0")
                session["auth"] = [p.decode() for p in blob]
            else:
                say("334 ")
                blob = base64.b64decode(read_line(f).strip()).split(b"\0")
                session["auth"] = [p.decode() for p in blob]
            authed = True
            say("235 Authentication successful")
        elif upper.startswith("AUTH LOGIN"):
            say("334 VXNlcm5hbWU6")
            user = base64.b64decode(read_line(f).strip()).decode()
            say("334 UGFzc3dvcmQ6")
            pw = base64.b64decode(read_line(f).strip()).decode()
            session["auth"] = ["", user, pw]
            authed = True
            say("235 Authentication successful")
        elif upper.startswith("MAIL FROM:"):
            if not authed:
                say("530 5.7.0 Authentication required")
                continue
            session["envelope_from"] = address_of(line[10:])
            say("250 2.1.0 Ok")
        elif upper.startswith("RCPT TO:"):
            who = address_of(line[8:])
            if args.reject_rcpt and who == args.reject_rcpt:
                say("550 5.1.1 <%s>: Recipient address rejected: no such user" % who)
                continue
            session["recipients"].append(who)
            say("250 2.1.5 Ok")
        elif upper == "DATA":
            say("354 End data with <CR><LF>.<CR><LF>")
            chunks = []
            while True:
                raw = read_line(f)
                if raw == b".\r\n":
                    break
                chunks.append(raw)
            # Store BOTH: the raw wire bytes, and the message after undoing
            # dot-stuffing, which is what a real MTA would deliver.
            wire = b"".join(chunks)
            undone = b"".join(c[1:] if c.startswith(b"..") else c for c in chunks)
            session["data_wire"] = wire.decode("utf-8", "surrogateescape")
            session["data"] = undone.decode("utf-8", "surrogateescape")
            say("250 2.0.0 Ok: queued as SINK1")
        elif upper == "QUIT":
            say("221 2.0.0 Bye")
            break
        elif upper == "RSET":
            say("250 2.0.0 Ok")
        else:
            say("502 5.5.2 Command not recognized")
    out.append(session)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--transcript", required=True)
    ap.add_argument("--count", type=int, default=1)
    ap.add_argument("--reject-rcpt")
    ap.add_argument("--require-auth", action="store_true")
    ap.add_argument("--tls", nargs=2)
    ap.add_argument("--starttls", nargs=2)
    args = ap.parse_args()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(8)
    print(srv.getsockname()[1], flush=True)

    if args.tls:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(args.tls[0], args.tls[1])

    out = []
    for _ in range(args.count):
        conn, _addr = srv.accept()
        if args.tls:
            try:
                conn = ctx.wrap_socket(conn, server_side=True)
            except ssl.SSLError as e:
                out.append({"tls_error": str(e)})
                conn.close()
                continue
        try:
            serve_one(conn, args, out)
        except Exception as e:               # a hung-up client is a result too
            out.append({"error": "%s: %s" % (type(e).__name__, e)})
        finally:
            try: conn.close()
            except Exception: pass
    with open(args.transcript, "w") as fh:
        json.dump(out, fh, indent=1)

if __name__ == "__main__":
    main()
